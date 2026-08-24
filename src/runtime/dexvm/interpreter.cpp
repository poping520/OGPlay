#include "ogplay/runtime/dexvm/interpreter.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <string_view>
#include <utility>

#include "interpreter_internal.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/vm_threads.h"

namespace ogplay::runtime::dexvm {

// ---- Impl helpers ----------------------------------------------------------

namespace {
constexpr std::uint32_t kAccSynchronized = 0x0020U;
constexpr std::size_t kMaximumFatalStackFrames = 64U;
constexpr std::string_view kGuestStackHeader =
    "\nDexVM guest stack (innermost first):";

struct FatalThreadInfo final {
    std::uint64_t guest_thread_id{};
    std::string name;
};

[[nodiscard]] std::optional<FatalThreadInfo> FindFatalThread(
    const VmThreadRuntime* const threads, const std::uint64_t context_token) {
    if (threads == nullptr) return std::nullopt;
    for (const auto& thread : threads->Snapshot()) {
        if (thread.context_token == context_token) {
            return FatalThreadInfo{thread.id, thread.name};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string EscapeThreadName(const std::string_view name) {
    constexpr std::size_t kMaximumThreadNameBytes = 128U;
    std::string escaped;
    const auto size = std::min(name.size(), kMaximumThreadNameBytes);
    for (std::size_t index = 0; index < size; ++index) {
        switch (name[index]) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\r': escaped += "\\r"; break;
            case '\n': escaped += "\\n"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(name[index]); break;
        }
    }
    if (name.size() > size) escaped += "...";
    return escaped;
}

[[nodiscard]] std::string HexByte(const std::uint8_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result{"0x00"};
    result[2] = digits[value >> 4U];
    result[3] = digits[value & 0x0fU];
    return result;
}

void AppendFaultInstruction(std::string& rendered, const Frame& frame) {
    if (!frame.method->code.has_value() ||
        frame.pc >= frame.method->code->instructions.size()) {
        return;
    }
    const auto& units = frame.method->code->instructions;
    const auto opcode = static_cast<std::uint8_t>(units[frame.pc] & 0xffU);
    const auto& info = gen::kDexOpcodeTable[opcode];
    rendered += "\nDexVM fault instruction: " + std::string(info.name) +
                " opcode=" + HexByte(opcode);
    if (info.index_type == gen::DexIndexType::method_ref &&
        frame.pc + 1U < units.size()) {
        rendered += " method_idx=" + std::to_string(units[frame.pc + 1U]);
    }
    rendered += " dex_pc=" + std::to_string(frame.pc);
}

[[nodiscard]] std::string RenderFatalErrorWithGuestStack(
    const DexVmError& error, const DexClassLinker& linker,
    const InterpreterExecutionState& execution,
    const VmThreadRuntime* const threads) {
    std::string rendered = error.what();
    if (rendered.find(kGuestStackHeader) != std::string::npos) {
        return rendered;
    }
    const auto& frames = execution.frames;
    const auto shown = std::min(frames.size(), kMaximumFatalStackFrames);
    rendered += kGuestStackHeader;
    rendered += " context=" + std::to_string(execution.token);
    if (const auto thread = FindFatalThread(threads, execution.token);
        thread.has_value()) {
        rendered += " guest_thread_id=" +
                    std::to_string(thread->guest_thread_id) + " thread=\"" +
                    EscapeThreadName(thread->name) + "\"";
    } else {
        rendered += " thread=<unregistered>";
    }
    rendered += " frames=" + std::to_string(frames.size()) +
                " shown=" + std::to_string(shown);
    if (!frames.empty()) AppendFaultInstruction(rendered, frames.back());
    auto frame = frames.rbegin();
    for (std::size_t index = 0; index < shown; ++index, ++frame) {
        const auto& method = *frame->method;
        rendered += "\n  #" + std::to_string(index) + " at " +
                    linker.Class(method.owner).descriptor + "->" + method.name +
                    method.descriptor + " (dex_pc=" +
                    std::to_string(frame->pc) + ")";
    }
    if (frames.size() > shown) {
        rendered += "\n  ... " + std::to_string(frames.size() - shown) +
                    " outer frames omitted";
    }
    return rendered;
}

// Neutral answer for a survey stub: zero/null of the declared return kind.
[[nodiscard]] VmValue NeutralValueFor(const char return_shorty) {
    switch (return_shorty) {
  case 'V':
    return VmValue::Void();
  case 'J':
    return VmValue::Long(0);
  case 'F':
    return VmValue::Float(0.0F);
  case 'D':
    return VmValue::Double(0.0);
  case 'L':
    return VmValue::Ref(VmObjectRef{});
  default:
    return VmValue::Int(0);
    }
}

}  // namespace

VmObjectRef Interpreter::Impl::MethodMonitor(
    const LinkedMethod& method,
    const std::span<const VmValue> arguments) const {
    if ((method.access_flags & kAccSynchronized) == 0U) return VmObjectRef(0);
    if (method.is_static) return model->ClassObject(method.owner);
    if (arguments.empty() || arguments.front().kind != VmValue::Kind::ref ||
        !arguments.front().ref.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "synchronized instance method has no receiver"};
    }
    return arguments.front().ref;
}

void Interpreter::Impl::ReleaseFrameMonitor(Frame& frame) noexcept {
    if (!frame.synchronized_monitor.IsValid()) return;
    try {
        monitors->Exit(frame.synchronized_monitor, Execution().token);
        RecordTrace(DexVmTraceKind::monitor_exit, Execution(), frame.method,
                    frame.pc, 0, frame.synchronized_monitor.Value());
    } catch (const std::exception&) {
    }
    frame.synchronized_monitor = VmObjectRef(0);
}

void Interpreter::Impl::PublishClinitState(const DexClassId java_class,
                                           const ClinitState state) {
    auto& linked = linker->MutableClass(java_class);
    linked.clinit_state = state;
    if (state != ClinitState::initializing) linked.clinit_thread = 0;
    clinit_generation.fetch_add(1U, std::memory_order_release);
    clinit_changed.notify_all();
}

void Interpreter::Impl::SetPending(const VmObjectRef throwable) {
    SetPendingExisting(throwable);
    if (!throwable.IsValid()) return;
    auto& state = throwables[throwable.Value()];
    if (state.stack.empty()) {
        state.stack = CaptureStack();
    }
}

void Interpreter::Impl::SetPendingExisting(const VmObjectRef throwable) {
    auto& execution = Execution();
    auto& pending_exception = execution.pending_exception;
    auto& pending_exception_class = execution.pending_exception_class;
    if (!throwable.IsValid()) {
        ThrowJava("Ljava/lang/NullPointerException;", "throw null");
        return;
    }
    pending_exception = throwable;
    pending_exception_class = model->ObjectClass(throwable);
    const auto* method = execution.frames.empty()
                             ? nullptr
                             : execution.frames.back().method;
    const auto pc = execution.frames.empty() ? 0U
                                             : execution.frames.back().pc;
    RecordTrace(DexVmTraceKind::exception_throw, execution, method, pc, 0,
                throwable.Value());
}

void Interpreter::Impl::ThrowJava(const std::string& descriptor,
                                  const std::string& message) {
    auto& execution = Execution();
    auto& pending_exception = execution.pending_exception;
    auto& pending_exception_class = execution.pending_exception_class;
    const auto throwable = owner->MakeThrowable(descriptor, message);
    pending_exception = throwable;
    pending_exception_class = model->ObjectClass(throwable);
    const auto* method = execution.frames.empty()
                             ? nullptr
                             : execution.frames.back().method;
    const auto pc = execution.frames.empty() ? 0U
                                             : execution.frames.back().pc;
    RecordTrace(DexVmTraceKind::exception_throw, execution, method, pc, 0,
                throwable.Value());
}

std::vector<VmStackEntry> Interpreter::Impl::CaptureStack() const {
    const auto& frames = Execution().frames;
    std::vector<VmStackEntry> stack;
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
        const auto& method = *it->method;
    stack.push_back(
        {linker->Class(method.owner).descriptor, method.name, it->pc});
    }
    return stack;
}

VmObjectRef Interpreter::Impl::AllocateInstance(const DexClassId java_class) {
    linker->EnsureClassLinked(java_class);
    const auto& linked = linker->Class(java_class);
    if (linked.is_interface || linked.is_array) {
        FailCode("cannot instantiate " + linked.descriptor);
    }
    // Intrinsic and interpreted classes share the vm_instance form so raw
    // iget/iput on intrinsic-declared fields (Configuration.keyboard etc.)
    // work uniformly; opaque host state lives in interpreter side tables.
    return model->NewInstance(java_class, linked.instance_slots);
}

VmObjectRef
Interpreter::Impl::InternDexString(const std::uint32_t string_index) {
    const auto& image = linker->Image();
    if (string_index >= image.strings.size()) {
        FailCode("string index out of range");
    }
    const auto& value = image.strings[string_index].value;
  return model->InternString(std::u16string_view(value.data(), value.size()));
}

void Interpreter::Impl::EnsureInitialized(
    InterpreterExecutionState& execution, const DexClassId java_class) {
    auto& frames = execution.frames;
    auto& pending_exception = execution.pending_exception;
    for (;;) {
      auto& observed = linker->MutableClass(java_class);
      switch (observed.clinit_state) {
        case ClinitState::initialized:
            return;
        case ClinitState::failed:
            ThrowJava("Ljava/lang/NoClassDefFoundError;",
              "class initialization previously failed: " + observed.descriptor);
            return;
        case ClinitState::initializing:
            if (observed.clinit_thread == execution.token) return;
            {
                const auto generation =
                    clinit_generation.load(std::memory_order_acquire);
                auto& lock = execution_lock;
                const auto depth = lock.ReleaseForBlocking();
                std::unique_lock wait_lock(clinit_wait_mutex);
                clinit_changed.wait(wait_lock, [&] {
                    return clinit_generation.load(std::memory_order_acquire) !=
                               generation ||
                           execution.stop_requested.load(
                               std::memory_order_relaxed);
                });
                wait_lock.unlock();
                lock.ReacquireAfterBlocking(depth);
                Tick(execution, 0);
            }
            continue;
        case ClinitState::uninitialized:
            break;
      }
      break;
    }
    // ResolveDescriptor/AddClass can reallocate the class vector. Never keep
    // a LinkedClass& across those calls; re-fetch by id after each mutation.
    const auto super = linker->MutableClass(java_class).super;
    linker->MutableClass(java_class).clinit_state = ClinitState::initializing;
    linker->MutableClass(java_class).clinit_thread = execution.token;
    RecordTrace(DexVmTraceKind::class_init_begin, execution, nullptr, 0, 0,
                java_class.Value());
    try {

    // Superclass first (interfaces are not initialized transitively).
    if (super.has_value()) {
        EnsureInitialized(execution, *super);
        if (pending_exception.IsValid()) {
            PublishClinitState(java_class, ClinitState::failed);
            RecordTrace(DexVmTraceKind::class_init_fail, execution, nullptr,
                        0, 0, java_class.Value());
            return;
        }
    }

    // Materialize static initial values before running <clinit>
    // (state machine follows AOSP vm/oo/Class.cpp dvmInitClass).
    const auto values = linker->StaticValues(linker->Class(java_class));
    const auto static_field_count =
        linker->Class(java_class).own_static_fields.size();
    for (std::size_t index = 0;
         index < values.size() && index < static_field_count; ++index) {
        const auto field_id = linker->Class(java_class).own_static_fields[index];
        const auto slot = linker->Field(field_id).slot;
        const auto& value = values[index];
        using loader::DexEncodedValueKind;
        auto write_slot = [&](const std::uint16_t offset,
                              const std::uint32_t bits) {
            linker->MutableClass(java_class)
                .static_storage[slot + offset] = bits;
        };
        switch (value.kind) {
            case DexEncodedValueKind::boolean_value:
            case DexEncodedValueKind::byte_value:
            case DexEncodedValueKind::short_value:
            case DexEncodedValueKind::char_value:
            case DexEncodedValueKind::int_value:
                write_slot(0, static_cast<std::uint32_t>(
                                  static_cast<std::int32_t>(value.integral)));
                break;
            case DexEncodedValueKind::long_value: {
                const auto bits = static_cast<std::uint64_t>(value.integral);
                write_slot(0, static_cast<std::uint32_t>(bits));
                write_slot(1, static_cast<std::uint32_t>(bits >> 32U));
                break;
            }
            case DexEncodedValueKind::float_value: {
                const auto narrowed = static_cast<float>(value.floating);
                write_slot(0, std::bit_cast<std::uint32_t>(narrowed));
                break;
            }
            case DexEncodedValueKind::double_value: {
                const auto bits = std::bit_cast<std::uint64_t>(value.floating);
                write_slot(0, static_cast<std::uint32_t>(bits));
                write_slot(1, static_cast<std::uint32_t>(bits >> 32U));
                break;
            }
            case DexEncodedValueKind::string_index:
                write_slot(0, InternDexString(value.index).Value());
                break;
            case DexEncodedValueKind::type_index:
                write_slot(0, model->ClassObject(
                                  linker->ResolveTypeIndex(value.index))
                                  .Value());
                break;
            case DexEncodedValueKind::null_reference:
                write_slot(0, 0);
                break;
        }
    }

    // Intrinsic constant statics materialize before any handler runs.
    const auto intrinsic_constants =
        linker->Class(java_class).intrinsic_constants;
    for (const auto& constant : intrinsic_constants) {
        const auto field_id = linker->FindFieldRecursive(
            java_class, constant.name, constant.descriptor);
        if (!field_id.has_value()) continue;
        const auto& field = linker->Field(*field_id);
        const auto slot = field.slot;
        const auto is_wide = field.is_wide;
        if (constant.descriptor == "Ljava/lang/String;") {
            std::u16string value(constant.string_value.begin(),
                                 constant.string_value.end());
            linker->MutableClass(java_class).static_storage[slot] =
                model->InternString(value).Value();
        } else if (is_wide) {
            const auto bits = static_cast<std::uint64_t>(constant.integral);
            auto& storage = linker->MutableClass(java_class).static_storage;
            storage[slot] = static_cast<std::uint32_t>(bits);
            storage[slot + 1] = static_cast<std::uint32_t>(bits >> 32U);
        } else {
            linker->MutableClass(java_class).static_storage[slot] =
                static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(constant.integral));
        }
    }
    const auto clinit_implementation =
        linker->Class(java_class).clinit_implementation;
    if (clinit_implementation) {
        IntrinsicContext context{*owner, VmObjectRef{}, {}};
        try {
            static_cast<void>(clinit_implementation(context));
        } catch (const VmJavaThrow& thrown) {
            PublishClinitState(java_class, ClinitState::failed);
            RecordTrace(DexVmTraceKind::class_init_fail, execution, nullptr,
                        0, 0, java_class.Value());
            ThrowJava(thrown.descriptor, thrown.message);
            return;
        }
    }

    const auto clinit = linker->Class(java_class).clinit;
    if (clinit.has_value()) {
        linker->PrecheckMethod(*clinit);
        PushInterpretedFrame(execution, linker->Method(*clinit), {}, 0);
        const auto outcome = Run(execution, frames.size() - 1);
        if (outcome.exception.IsValid()) {
            PublishClinitState(java_class, ClinitState::failed);
            RecordTrace(DexVmTraceKind::class_init_fail, execution, nullptr,
                        0, 0, java_class.Value());
            // Initialization failure is sticky NoClassDefFoundError for
            // later users; the original throwable propagates now.
            SetPending(outcome.exception);
            return;
        }
    }
    PublishClinitState(java_class, ClinitState::initialized);
    RecordTrace(DexVmTraceKind::class_init_end, execution, nullptr, 0, 0,
                java_class.Value());
    ++stats.classes_initialized;
    } catch (...) {
        auto& failed = linker->MutableClass(java_class);
        if (failed.clinit_state == ClinitState::initializing &&
            failed.clinit_thread == execution.token) {
            PublishClinitState(java_class, ClinitState::failed);
            RecordTrace(DexVmTraceKind::class_init_fail, execution, nullptr,
                        0, 0, java_class.Value());
        }
        throw;
    }
}

void Interpreter::Impl::CheckInvokeWidePair(
    const Frame& frame, const std::uint32_t lo,
    const std::optional<std::uint32_t> listed_hi,
    const bool require_listed_consecutive) {
    const auto where = linker->Class(frame.method->owner).descriptor + "." +
                       frame.method->name + " pc " +
                       std::to_string(frame.pc);
    if (lo + 1U >= frame.regs.size()) {
        throw DexVmError(DexVmErrorReason::invalid_register,
                         where + ": register out of range");
    }
    if (require_listed_consecutive) {
        if (!listed_hi.has_value() || *listed_hi != lo + 1U) {
            throw DexVmError(
                DexVmErrorReason::invalid_register,
                where + ": wide invoke argument is not a consecutive pair");
        }
    }
}

void Interpreter::Impl::PushInterpretedFrame(
    InterpreterExecutionState& execution, const LinkedMethod& method,
    const std::span<const VmValue> arguments,
    const std::uint32_t caller_advance) {
    auto& frames = execution.frames;
    if (frames.size() >= config.max_frames) {
        throw VmJavaThrow{"Ljava/lang/StackOverflowError;",
                          "frame depth " + std::to_string(frames.size())};
    }
    const auto& code = *method.code;
    Frame frame;
    frame.method = &method;
    frame.fast_ip = 0;
    frame.synchronized_monitor = MethodMonitor(method, arguments);
    frame.regs.assign(code.info.registers_size, Slot{});
    // Arguments occupy the trailing registers (Dalvik ins convention).
    std::uint32_t reg =
      static_cast<std::uint32_t>(code.info.registers_size) - method.ins_words;
    for (const auto& value : arguments) {
        switch (value.kind) {
            case VmValue::Kind::cat1:
                frame.regs[reg] = {value.cat1, SlotTag::cat1};
                reg += 1;
                break;
            case VmValue::Kind::wide:
                frame.regs[reg] = {static_cast<std::uint32_t>(value.wide),
                                   SlotTag::wide_lo};
      frame.regs[reg + 1] = {static_cast<std::uint32_t>(value.wide >> 32U),
                    SlotTag::wide_hi};
                reg += 2;
                break;
            case VmValue::Kind::ref:
                frame.regs[reg] = {value.ref.Value(), SlotTag::ref};
                reg += 1;
                break;
            case VmValue::Kind::void_value:
                FailCode("void argument in invoke marshaling");
        }
    }
    const auto synchronized_monitor = frame.synchronized_monitor;
    if (synchronized_monitor.IsValid()) {
        monitors->Enter(synchronized_monitor, execution.token);
        RecordTrace(DexVmTraceKind::monitor_enter, execution, &method, 0, 0,
                    synchronized_monitor.Value());
    }
    try {
        frames.push_back(std::move(frame));
        if (frames.size() > 1U) {
            frames[frames.size() - 2U].pending_advance = caller_advance;
        }
    } catch (...) {
        if (synchronized_monitor.IsValid()) {
            monitors->Exit(synchronized_monitor, execution.token);
        }
        throw;
    }
    ++stats.method_calls;
    RecordTrace(DexVmTraceKind::method_enter, execution, &method, 0);
}

VmValue
Interpreter::Impl::InvokeIntrinsic(const LinkedMethod &method,
                                   const VmObjectRef receiver,
    const std::span<const VmValue> arguments) {
    const auto handler = method.implementation;
    const auto owner_id = method.owner;
    const auto name = method.name;
    const auto descriptor = method.descriptor;
    const auto return_shorty = method.return_shorty;
    if (!handler) {
        const auto& declaring = linker->Class(owner_id).descriptor;
        const auto diagnostic = declaring + "." + name + descriptor;
        if (ledger != nullptr) {
            ledger->RecordUnimplemented("dexvm.intrinsic." + diagnostic, 0);
        }
        if (linker->GapSurveyEnabled()) {
            // Survey mode: record and answer neutrally so one run harvests
            // every gap the title reaches. Never a compatibility result.
            linker->RecordGapSurveyHit(declaring, name + descriptor);
            if (logger != nullptr) {
                logger->Write(core::LogLevel::warn, "runtime.dexvm.survey",
                              "SURVEY neutral stub: " + declaring + "." + name +
                                  descriptor);
            }
            return NeutralValueFor(return_shorty);
        }
        throw VmJavaThrow{"Ljava/lang/UnsatisfiedLinkError;",
                          "intrinsic handler is not implemented: " +
                              diagnostic};
    }
    ++stats.intrinsic_calls;
    IntrinsicContext context{*owner, receiver, arguments};
    return handler(context);
}

VmCallOutcome Interpreter::Impl::Run(InterpreterExecutionState& execution,
                                     const std::size_t entry_depth) {
    auto& frames = execution.frames;
    auto& pending_exception = execution.pending_exception;
    auto& pending_exception_class = execution.pending_exception_class;
    auto& exit_result = execution.exit_result;
    VmCallOutcome outcome;
    while (frames.size() > entry_depth) {
        try {
            if (config.backend == InterpreterBackend::threaded) {
                StepThreaded(execution);
            } else {
                Step(execution);
            }
        } catch (const VmJavaThrow& thrown) {
            ThrowJava(thrown.descriptor, thrown.message);
        } catch (const DexVmError& error) {
            if (error.Reason() == DexVmErrorReason::heap_budget_exhausted) {
                // The OutOfMemoryError object itself must still allocate;
                // a bounded emergency reserve keeps this honest instead of
                // crashing while reporting the exhaustion.
                model->SetEmergencyReserve(true);
                ThrowJava("Ljava/lang/OutOfMemoryError;", error.what());
                model->SetEmergencyReserve(false);
      } else if (error.Reason() == DexVmErrorReason::object_model_failure &&
                       linker->GapSurveyEnabled()) {
                // Survey mode is diagnostic-only: a neutral stub's null/zero
                // flowed into a host accessor. Surface it as a guest
                // NullPointerException so the one survey run keeps harvesting
                // gaps instead of aborting the process. Never a
                // compatibility result (the run is loudly flagged).
                if (logger != nullptr) {
                    logger->Write(core::LogLevel::warn, "runtime.dexvm.survey",
                                  std::string("SURVEY neutral-stub fault: ") +
                                      error.what());
                }
                ThrowJava("Ljava/lang/NullPointerException;", error.what());
            } else {
                const auto rendered = RenderFatalErrorWithGuestStack(
                    error, *linker, execution, threads);
                // The call is over: drop its frames so the context stays
                // usable, and discardable once its thread is joined.
                while (frames.size() > entry_depth) {
                    RecordTrace(DexVmTraceKind::method_exit, execution,
                                frames.back().method, frames.back().pc, 0, 1);
                    ReleaseFrameMonitor(frames.back());
                    frames.pop_back();
                }
                throw DexVmError(error.Reason(), rendered);
            }
        }
    if (!pending_exception.IsValid())
      continue;

        // Exception unwinding (02 §8; catch matching follows AOSP
        // vm/Exception.cpp dvmFindCatchBlock: declaration order, then
        // catch-all, then pop).
        while (frames.size() > entry_depth) {
            auto& frame = frames.back();
            const auto& method = *frame.method;
            bool handled = false;
            if (method.code.has_value()) {
                for (const auto& block : method.code->tries) {
                    if (frame.pc < block.start_pc ||
                        frame.pc >= block.start_pc + block.instruction_count) {
                        continue;
                    }
                    for (const auto& handler : block.typed_handlers) {
            const auto handler_class =
                linker->ResolveTypeIndex(handler.type_index);
            if (linker->IsAssignable(handler_class, pending_exception_class)) {
                            frame.pc = handler.handler_pc;
                            frame.fast_ip = kInvalidFastIndex;
                            handled = true;
                            break;
                        }
                    }
                    if (!handled && block.catch_all_pc.has_value()) {
                        frame.pc = *block.catch_all_pc;
                        frame.fast_ip = kInvalidFastIndex;
                        handled = true;
                    }
          if (handled)
            break;
                }
            }
            if (handled) {
                RecordTrace(DexVmTraceKind::exception_catch, execution,
                            frame.method, frame.pc, 0,
                            pending_exception.Value());
                frame.caught = pending_exception;
                pending_exception = VmObjectRef{};
                pending_exception_class = DexClassId{};
                break;
            }
            RecordTrace(DexVmTraceKind::method_exit, execution, frame.method,
                        frame.pc, 0, 1);
            ReleaseFrameMonitor(frame);
            frames.pop_back();
        }
        if (pending_exception.IsValid() && frames.size() <= entry_depth) {
            outcome.exception = pending_exception;
            outcome.exception_class = pending_exception_class;
            const auto state = throwables.find(pending_exception.Value());
            if (state != throwables.end()) {
                outcome.exception_message = state->second.message_utf8;
                outcome.exception_stack = state->second.stack;
            }
            pending_exception = VmObjectRef{};
            pending_exception_class = DexClassId{};
            return outcome;
        }
    }
    outcome.value = exit_result;
    return outcome;
}

// ---- public API ------------------------------------------------------------

Interpreter::Interpreter(DexClassLinker& linker, JavaObjectModel& model,
                         NativeMethodBridge* bridge,
                         core::CapabilityLedger& ledger,
                         const InterpreterConfig config)
    : impl_(std::make_unique<Impl>()) {
    impl_->linker = &linker;
    impl_->model = &model;
    impl_->bridge = bridge;
    impl_->ledger = &ledger;
    impl_->config = config;
    impl_->stats.backend = config.backend;
    if (config.diagnostics.instruction_sample_interval == 0U) {
        throw std::invalid_argument(
            "DexVM instruction trace sample interval must be non-zero");
    }
    if (config.diagnostics.trace_capacity > 1'000'000U) {
        throw std::invalid_argument(
            "DexVM trace capacity must not exceed 1000000 entries");
    }
    impl_->trace_ring.resize(config.diagnostics.trace_capacity);
    impl_->owner = this;
    auto default_execution = std::make_unique<InterpreterExecutionState>();
    default_execution->token = 1;
    impl_->default_execution = default_execution.get();
    impl_->executions.emplace(1, std::move(default_execution));
    impl_->monitors = std::make_unique<VmMonitorTable>(*this);
    model.SetClassDescriptorResolver(
        [&linker](const DexClassId java_class) {
            return linker.Class(java_class).descriptor;
        });

    RegisterIntrinsicStateTable({
        "throwable",
        [state = impl_.get()](const VmObjectRef owner,
                              const VmRootVisitor& visit) {
            const auto found = state->throwables.find(owner.Value());
            if (found == state->throwables.end()) return;
            visit(found->second.message);
            visit(found->second.cause);
        },
        [state = impl_.get()](const VmObjectRef owner) {
            state->throwables.erase(owner.Value());
        },
        {}});
    RegisterIntrinsicStateTable({
        "builder", {},
        [state = impl_.get()](const VmObjectRef owner) {
            state->builders.erase(owner.Value());
        },
        [state = impl_.get()](const VmObjectRef source,
                              const VmObjectRef clone) {
            const auto found = state->builders.find(source.Value());
            if (found != state->builders.end()) {
                state->builders[clone.Value()] = found->second;
            }
        }});
    RegisterIntrinsicStateTable({
        "collections",
        [state = impl_.get()](const VmObjectRef owner,
                              const VmRootVisitor& visit) {
            state->collections.Trace(owner, visit);
        },
        [state = impl_.get()](const VmObjectRef owner) {
            state->collections.Sweep(owner);
        },
        [state = impl_.get()](const VmObjectRef source,
                              const VmObjectRef clone) {
            state->collections.Clone(source, clone);
        }});
    RegisterIntrinsicStateTable({
        "io",
        [](const VmObjectRef, const VmRootVisitor&) {},
        [state = impl_.get()](const VmObjectRef owner) {
            state->io.Sweep(owner);
        },
        [](const VmObjectRef, const VmObjectRef) {}});

    const auto string_class = linker.FindClass("Ljava/lang/String;");
    const auto class_class = linker.FindClass("Ljava/lang/Class;");
    if (string_class.has_value() && class_class.has_value()) {
        model.SetCoreClasses(*string_class, *class_class);
    }
    impl_->class_loaders =
        std::make_unique<ClassLoaderFacade>(linker, model);
    impl_->reflection =
        std::make_unique<ReflectionRuntime>(*this, linker, model);
}

CollectionRuntime& Interpreter::Collections() {
    return impl_->collections;
}

const CollectionRuntime& Interpreter::Collections() const {
    return impl_->collections;
}

IoRuntime& Interpreter::IO() { return impl_->io; }

const IoRuntime& Interpreter::IO() const { return impl_->io; }

Interpreter::~Interpreter() = default;

ClassLoaderFacade& Interpreter::ClassLoaders() noexcept {
    return *impl_->class_loaders;
}

ReflectionRuntime& Interpreter::Reflection() noexcept {
    return *impl_->reflection;
}

VmCallOutcome Interpreter::Call(const VmMethodId method_id,
                                const std::span<const VmValue> arguments) {
    VmExecutionLockScope lock_scope(impl_->execution_lock);
    auto& execution = impl_->Execution();
    InterpreterExecutionScope execution_scope(impl_.get(), execution);
    auto& frames = execution.frames;
    auto& pending_exception = execution.pending_exception;
    auto& pending_exception_class = execution.pending_exception_class;
    if (frames.empty()) {
        // The tick budget is per top-level lifecycle entry call (04 §6);
        // nested calls (native -> interpreter re-entry) share the budget.
        execution.ticks = 0;
    }
    const auto owner = impl_->linker->Method(method_id).owner;
    impl_->linker->EnsureClassLinked(owner);
    if (impl_->linker->Method(method_id).is_static) {
        impl_->EnsureInitialized(execution, owner);
        if (pending_exception.IsValid()) {
            VmCallOutcome outcome;
            outcome.exception = pending_exception;
            outcome.exception_class = pending_exception_class;
            pending_exception = VmObjectRef{};
            pending_exception_class = DexClassId{};
            return outcome;
        }
    }
    const auto kind = impl_->linker->Method(method_id).kind;
    switch (kind) {
        case MethodKind::interpreted: {
            impl_->linker->PrecheckMethod(method_id);
            const auto entry_depth = frames.size();
            impl_->PushInterpretedFrame(execution,
                                        impl_->linker->Method(method_id),
                                        arguments, 0);
            auto outcome = impl_->Run(execution, entry_depth);
            return outcome;
        }
        case MethodKind::intrinsic: {
            const auto is_static = impl_->linker->Method(method_id).is_static;
            VmCallOutcome outcome;
            const auto receiver = is_static || arguments.empty()
                                      ? VmObjectRef{}
                                      : arguments.front().ref;
            const auto rest = is_static ? arguments : arguments.subspan(1);
            const Impl::MethodMonitorScope monitor(
                *impl_, impl_->linker->Method(method_id), arguments);
            impl_->RecordTrace(DexVmTraceKind::method_enter, execution,
                               &impl_->linker->Method(method_id));
            try {
                outcome.value = impl_->InvokeIntrinsic(
                    impl_->linker->Method(method_id), receiver, rest);
                impl_->RecordTrace(DexVmTraceKind::method_exit, execution,
                                   &impl_->linker->Method(method_id));
            } catch (const VmJavaThrow& thrown) {
                impl_->ThrowJava(thrown.descriptor, thrown.message);
                impl_->RecordTrace(DexVmTraceKind::method_exit, execution,
                                   nullptr, 0, 0, 1);
                outcome.exception = pending_exception;
                outcome.exception_class = pending_exception_class;
                outcome.exception_message = thrown.message;
                pending_exception = VmObjectRef{};
                pending_exception_class = DexClassId{};
            } catch (...) {
                impl_->RecordTrace(DexVmTraceKind::method_exit, execution,
                                   nullptr, 0, 0, 1);
                throw;
            }
            if (pending_exception.IsValid()) {
                outcome.exception = pending_exception;
                outcome.exception_class = pending_exception_class;
                const auto state =
                    impl_->throwables.find(pending_exception.Value());
                if (state != impl_->throwables.end()) {
                    outcome.exception_message = state->second.message_utf8;
                    outcome.exception_stack = state->second.stack;
                }
                pending_exception = VmObjectRef{};
                pending_exception_class = DexClassId{};
            }
            return outcome;
        }
        case MethodKind::native: {
            if (impl_->bridge == nullptr) {
                if (impl_->ledger != nullptr) {
                    impl_->ledger->RecordUnimplemented("dexvm.native_bridge", 0);
                }
                const auto& method = impl_->linker->Method(method_id);
                throw DexVmError(
                    DexVmErrorReason::native_bridge_unavailable,
                    "native method requires the JNI bridge: " +
                        impl_->linker->Class(method.owner).descriptor + "." +
                        method.name);
            }
            const auto is_static = impl_->linker->Method(method_id).is_static;
            VmCallOutcome outcome;
            const auto receiver = is_static || arguments.empty()
                                      ? VmObjectRef{}
                                      : arguments.front().ref;
            const auto rest = is_static ? arguments : arguments.subspan(1);
            ++impl_->stats.native_calls;
            const Impl::MethodMonitorScope monitor(
                *impl_, impl_->linker->Method(method_id), arguments);
            const Impl::NativeFrame native_frame(*impl_);
            impl_->RecordTrace(DexVmTraceKind::native_enter, execution,
                               &impl_->linker->Method(method_id));
            impl_->RecordTrace(DexVmTraceKind::method_enter, execution,
                               &impl_->linker->Method(method_id));
            try {
                outcome.value = impl_->bridge->Invoke(
                    impl_->linker->Method(method_id), receiver, rest);
                impl_->RecordTrace(DexVmTraceKind::native_exit, execution,
                                   &impl_->linker->Method(method_id));
                impl_->RecordTrace(DexVmTraceKind::method_exit, execution,
                                   &impl_->linker->Method(method_id));
            } catch (...) {
                impl_->RecordTrace(DexVmTraceKind::native_exit, execution,
                                   nullptr, 0, 0, 1);
                impl_->RecordTrace(DexVmTraceKind::method_exit, execution,
                                   nullptr, 0, 0, 1);
                throw;
            }
            return outcome;
        }
        case MethodKind::abstract:
            throw DexVmError(DexVmErrorReason::invalid_member,
                             "abstract method invoked directly: " +
                                 impl_->linker->Method(method_id).name);
    }
    throw DexVmError(DexVmErrorReason::internal_invariant,
                     "unreachable method kind");
}

VmCallOutcome Interpreter::Call(
    const InterpreterExecutionContext& context, const VmMethodId method_id,
    const std::span<const VmValue> arguments) {
    VmExecutionLockScope lock_scope(impl_->execution_lock);
    auto& execution = impl_->Execution(context);
    InterpreterExecutionScope execution_scope(impl_.get(), execution);
    return Call(method_id, arguments);
}

VmCallOutcome Interpreter::EnsureClassInitialized(const DexClassId java_class) {
    VmExecutionLockScope lock_scope(impl_->execution_lock);
    auto& execution = impl_->Execution();
    InterpreterExecutionScope execution_scope(impl_.get(), execution);
    auto& pending_exception = execution.pending_exception;
    auto& pending_exception_class = execution.pending_exception_class;
    impl_->linker->EnsureClassLinked(java_class);
    impl_->EnsureInitialized(execution, java_class);
    VmCallOutcome outcome;
    if (pending_exception.IsValid()) {
        outcome.exception = pending_exception;
        outcome.exception_class = pending_exception_class;
    const auto state = impl_->throwables.find(pending_exception.Value());
        if (state != impl_->throwables.end()) {
            outcome.exception_message = state->second.message_utf8;
            outcome.exception_stack = state->second.stack;
        }
        pending_exception = VmObjectRef{};
        pending_exception_class = DexClassId{};
    }
    return outcome;
}

VmCallOutcome Interpreter::EnsureClassInitialized(
    const InterpreterExecutionContext& context, const DexClassId java_class) {
    VmExecutionLockScope lock_scope(impl_->execution_lock);
    auto& execution = impl_->Execution(context);
    InterpreterExecutionScope execution_scope(impl_.get(), execution);
    return EnsureClassInitialized(java_class);
}

DexClassLinker& Interpreter::Linker() noexcept { return *impl_->linker; }
JavaObjectModel& Interpreter::Model() noexcept { return *impl_->model; }
const InterpreterStats& Interpreter::Stats() const noexcept {
    return impl_->stats;
}

VmObjectRef Interpreter::NewStringUtf8(const std::string_view utf8) {
    std::u16string units;
    units.reserve(utf8.size());
    // Strict ASCII fast path; multi-byte UTF-8 decoded checked.
    std::size_t index = 0;
    while (index < utf8.size()) {
        const auto byte = static_cast<std::uint8_t>(utf8[index]);
        if (byte < 0x80) {
            units.push_back(byte);
            index += 1;
        } else if ((byte & 0xe0U) == 0xc0U && index + 1 < utf8.size()) {
            units.push_back(static_cast<char16_t>(
                ((byte & 0x1fU) << 6U) |
                (static_cast<std::uint8_t>(utf8[index + 1]) & 0x3fU)));
            index += 2;
        } else if ((byte & 0xf0U) == 0xe0U && index + 2 < utf8.size()) {
            units.push_back(static_cast<char16_t>(
                ((byte & 0x0fU) << 12U) |
          ((static_cast<std::uint8_t>(utf8[index + 1]) & 0x3fU) << 6U) |
                (static_cast<std::uint8_t>(utf8[index + 2]) & 0x3fU)));
            index += 3;
        } else {
            throw DexVmError(DexVmErrorReason::invalid_operand,
                             "invalid UTF-8 in string literal");
        }
    }
    return impl_->model->NewString(units);
}

std::string Interpreter::StringUtf8(const VmObjectRef string_ref) const {
    const auto value = impl_->model->StringValue(string_ref);
    std::string out;
    out.reserve(value.size());
    for (const auto unit : value) {
        if (unit < 0x80) {
            out.push_back(static_cast<char>(unit));
        } else if (unit < 0x800) {
            out.push_back(static_cast<char>(0xc0U | (unit >> 6U)));
            out.push_back(static_cast<char>(0x80U | (unit & 0x3fU)));
        } else {
            out.push_back(static_cast<char>(0xe0U | (unit >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((unit >> 6U) & 0x3fU)));
            out.push_back(static_cast<char>(0x80U | (unit & 0x3fU)));
        }
    }
    return out;
}

VmObjectRef Interpreter::MakeThrowable(const std::string_view descriptor,
                                       const std::string_view message) {
    const auto java_class = impl_->linker->FindClass(descriptor);
    if (!java_class.has_value()) {
        throw DexVmError(DexVmErrorReason::unknown_class,
                         "throwable class is not registered: " +
                             std::string(descriptor));
    }
    const auto throwable = impl_->AllocateInstance(*java_class);
    auto& state = impl_->throwables[throwable.Value()];
    state.message_utf8 = std::string(message);
    if (!message.empty()) {
        state.message = NewStringUtf8(message);
    }
    state.stack = impl_->CaptureStack();
    return throwable;
}

void Interpreter::SetPendingException(const VmObjectRef throwable) {
    impl_->SetPendingExisting(throwable);
}

void Interpreter::SetThrowableMessage(const VmObjectRef throwable,
                                      const VmObjectRef message) {
    auto& state = impl_->throwables[throwable.Value()];
    state.message = message;
  state.message_utf8 = message.IsValid() ? StringUtf8(message) : std::string{};
}

VmObjectRef Interpreter::ThrowableMessage(const VmObjectRef throwable) const {
    const auto state = impl_->throwables.find(throwable.Value());
  if (state == impl_->throwables.end())
    return VmObjectRef{};
    return state->second.message;
}

void Interpreter::SetLogger(core::Logger* logger) noexcept {
    impl_->logger = logger;
}
core::Logger* Interpreter::Log() const noexcept { return impl_->logger; }

VmObjectRef Interpreter::CloneObject(const VmObjectRef source) {
    const auto clone = impl_->model->CloneObject(source);
    for (const auto& table : impl_->intrinsic_state_tables) {
        if (table.clone) table.clone(source, clone);
    }
    return clone;
}

std::u16string& Interpreter::BuilderBuffer(const VmObjectRef instance) {
    return impl_->builders[instance.Value()];
}
std::vector<VmObjectRef>& Interpreter::ListStorage(
    const VmObjectRef instance) {
    return impl_->collections.EnsureSequence(instance).elements;
}
std::optional<std::string> Interpreter::GetSystemProperty(
    const std::string_view key) const {
    const auto found = impl_->system_properties.find(std::string(key));
    if (found == impl_->system_properties.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<std::string> Interpreter::SetSystemProperty(
    std::string key, std::string value) {
    const auto found = impl_->system_properties.find(key);
    std::optional<std::string> previous;
    if (found != impl_->system_properties.end()) {
        previous = found->second;
    }
    impl_->system_properties[std::move(key)] = std::move(value);
    return previous;
}

void Interpreter::SetIntrinsicStaticRef(const std::string_view class_descriptor,
                                        const std::string_view field_name,
                                        const std::string_view field_descriptor,
                                        const VmObjectRef value) {
    const auto java_class = impl_->linker->FindClass(class_descriptor);
    if (!java_class.has_value()) {
        throw DexVmError(DexVmErrorReason::unknown_class,
                         "intrinsic static owner is not registered: " +
                             std::string(class_descriptor));
    }
    const auto field_id = impl_->linker->FindFieldRecursive(
        *java_class, std::string(field_name), std::string(field_descriptor));
    if (!field_id.has_value()) {
        throw DexVmError(DexVmErrorReason::invalid_member,
                         "intrinsic static field is not declared: " +
                             std::string(field_name));
    }
    const auto& field = impl_->linker->Field(*field_id);
    impl_->linker->MutableClass(field.owner).static_storage[field.slot] =
        value.Value();
}

void Interpreter::SetStaticFieldBits(const std::string_view class_descriptor,
    const std::string_view field_name,
    const std::string_view field_descriptor,
    const std::uint64_t bits) {
    const auto java_class = impl_->linker->FindClass(class_descriptor);
    if (!java_class.has_value()) {
        throw DexVmError(DexVmErrorReason::unknown_class,
                         "preset static owner is not in the dex: " +
                             std::string(class_descriptor));
    }
    const auto field_id = impl_->linker->FindFieldRecursive(
        *java_class, std::string(field_name), std::string(field_descriptor));
    if (!field_id.has_value()) {
    throw DexVmError(
        DexVmErrorReason::invalid_member,
        "preset static field is not declared: " + std::string(field_name) +
                             std::string(field_descriptor));
    }
    const auto& field = impl_->linker->Field(*field_id);
    if (!field.is_static) {
        throw DexVmError(DexVmErrorReason::invalid_member,
                     "preset target is not static: " + std::string(field_name));
    }
    auto& owner = impl_->linker->MutableClass(field.owner);
    if (owner.clinit_state != ClinitState::initialized) {
        throw DexVmError(DexVmErrorReason::clinit_failure,
                         "preset target class is not initialized: " +
                             owner.descriptor);
    }
  if (field.slot + (field.is_wide ? 2U : 1U) > owner.static_storage.size()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "preset static slot is out of range: " +
                             std::string(field_name));
    }
    owner.static_storage[field.slot] = static_cast<std::uint32_t>(bits);
    if (field.is_wide) {
        owner.static_storage[field.slot + 1U] =
            static_cast<std::uint32_t>(bits >> 32U);
    }
}

VmObjectRef
Interpreter::NewIntrinsicInstance(const std::string_view class_descriptor) {
    const auto java_class = impl_->linker->FindClass(class_descriptor);
    if (!java_class.has_value()) {
        throw DexVmError(DexVmErrorReason::unknown_class,
                         "intrinsic class is not registered: " +
                             std::string(class_descriptor));
    }
    return impl_->AllocateInstance(*java_class);
}

bool Interpreter::JavaEquals(const VmObjectRef left,
                             const VmObjectRef right) const {
  if (left == right)
    return true;
  if (!left.IsValid() || !right.IsValid())
    return false;
    const auto left_kind = impl_->model->Kind(left);
    const auto right_kind = impl_->model->Kind(right);
    const auto is_string = [](const VmObjectKind kind) {
        return kind == VmObjectKind::string || kind == VmObjectKind::external;
    };
    if (is_string(left_kind) && is_string(right_kind)) {
        try {
            return impl_->model->StringValue(left) ==
                   impl_->model->StringValue(right);
        } catch (const JniStringError&) {
            // External identities that are not strings compare by identity.
            return false;
        }
    }
    return false;
}

}  // namespace ogplay::runtime::dexvm
