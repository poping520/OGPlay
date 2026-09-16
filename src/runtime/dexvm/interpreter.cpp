#include "ogplay/runtime/dexvm/interpreter.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <string_view>
#include <utility>

#include "interpreter_internal.h"

#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/core/encoding.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/vm_threads.h"

namespace ogplay::runtime::dexvm {

// ---- Impl helpers ----------------------------------------------------------

namespace {
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
    std::string result{"0x00"};
    result[2] = core::HexDigit(value >> 4U, core::HexCase::lower);
    result[3] = core::HexDigit(value, core::HexCase::lower);
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
                " (opcode=" + HexByte(opcode);
    if (info.index_type == gen::DexIndexType::method_ref &&
        frame.pc + 1U < units.size()) {
        rendered += ", method_idx=" + std::to_string(units[frame.pc + 1U]);
    }
    rendered += ", dex_pc=" + std::to_string(frame.pc) + ")";
}

[[nodiscard]] std::string RenderFatalErrorWithGuestStack(
    const DexVmError& error, const DexClassLinker& linker,
    Interpreter& vm,
    const InterpreterExecutionState& execution,
    const VmThreadRuntime* const threads) {
    std::string rendered = error.what();
    if (rendered.find(kGuestStackHeader) != std::string::npos) {
        return rendered;
    }
    const auto& frames = execution.frames;
    const auto shown = std::min(frames.size(), kMaximumFatalStackFrames);
    rendered += "\nDexVM guest context: context=" +
                std::to_string(execution.token);
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
    if (!frames.empty()) {
        // toString diagnostics may push an interpreted frame and reallocate
        // execution.frames, so retain a stable copy of the fault registers.
        const auto fault_frame = frames.back();
        AppendFaultInstruction(rendered, fault_frame);
        AppendFaultInvokeArguments(rendered, fault_frame, vm);
    }
    rendered += kGuestStackHeader;
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
Interpreter::Impl::InternDexString(const DexUnitId unit,
                                   const std::uint32_t string_index) {
    const auto& image = linker->Image(unit);
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
    const auto dex_unit = linker->Class(java_class).dex_unit;
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
                write_slot(0,
                           InternDexString(*dex_unit, value.index).Value());
                break;
            case DexEncodedValueKind::type_index:
                write_slot(0, model->ClassObject(
                                  linker->ResolveTypeIndex(*dex_unit,
                                                           value.index))
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
            if (thrown.existing.IsValid()) owner->SetPendingException(thrown.existing);
            else ThrowJava(thrown.descriptor, thrown.message);
        }
    }

    const auto clinit = linker->Class(java_class).clinit;
    if (!clinit_implementation && clinit.has_value()) {
        linker->PrecheckMethod(*clinit);
        PushInterpretedFrame(execution, linker->Method(*clinit), {}, 0);
        const auto outcome = Run(execution, frames.size() - 1);
        if (outcome.exception.IsValid()) SetPending(outcome.exception);
    }
    if (pending_exception.IsValid()) {
        // AOSP Class.cpp / Exception.cpp: wrap non-Error initialization
        // failures, preserve their identity as cause, and leave Errors intact.
        const auto original = pending_exception;
        const auto error = linker->ResolveDescriptor("Ljava/lang/Error;");
        if (!linker->IsAssignable(error, model->ObjectClass(original))) {
            const auto original_root = owner->ProtectReferences(std::array{original});
            pending_exception = VmObjectRef{};
            execution.pending_exception_class = DexClassId{};
            const auto wrapper = owner->MakeThrowable("Ljava/lang/ExceptionInInitializerError;", "");
            const auto wrapper_root = owner->ProtectReferences(std::array{wrapper});
            const auto ctor = linker->FindDirectMethod(model->ObjectClass(wrapper), "<init>",
                                                       "(Ljava/lang/Throwable;)V");
            if (!ctor) throw DexVmError(DexVmErrorReason::internal_invariant,
                                       "missing ExceptionInInitializerError constructor");
            const auto result = owner->Call(*ctor, std::array{VmValue::Ref(wrapper), VmValue::Ref(original)});
            SetPending(result.exception.IsValid() ? result.exception : wrapper);
        }
        PublishClinitState(java_class, ClinitState::failed);
        RecordTrace(DexVmTraceKind::class_init_fail, execution, nullptr, 0, 0, java_class.Value());
        return;
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
    // Reserve a small tail so a full Java stack can still construct its error.
    const auto frame_limit = static_cast<std::size_t>(config.max_frames) +
                             (execution.constructing_throwable ? 32U : 0U);
    if (frames.size() >= frame_limit) {
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

VmValue Interpreter::Impl::InvokeIntrinsic(
    const LinkedMethod& method, const VmObjectRef receiver,
    const std::span<const VmValue> arguments) {
    const auto handler = method.implementation;
    const auto owner_id = method.owner;
    const auto name = method.name;
    const auto descriptor = method.descriptor;
    const auto return_shorty = method.return_shorty;
    const auto vm_kind = [](const ValueKind kind) {
        switch (kind) {
        case ValueKind::cat1: return VmValue::Kind::cat1;
        case ValueKind::wide: return VmValue::Kind::wide;
        case ValueKind::ref: return VmValue::Kind::ref;
        case ValueKind::void_value: return VmValue::Kind::void_value;
        }
        return VmValue::Kind::void_value;
    };
    if (!method.is_static) {
        if (!receiver.IsValid()) {
            throw DexVmError(DexVmErrorReason::internal_invariant,
                             "intrinsic receiver is null: " + name +
                                 descriptor);
        }
        const auto actual = model->ObjectClass(receiver);
        if (!actual.IsValid() || !linker->IsAssignable(method.owner, actual)) {
            throw DexVmError(DexVmErrorReason::internal_invariant,
                             "intrinsic receiver type mismatch: " + name +
                                 descriptor);
        }
    }
    if (arguments.size() != method.shape.parameter_kinds.size()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "intrinsic argument count mismatch: " + name +
                             descriptor);
    }
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (arguments[index].kind !=
            vm_kind(method.shape.parameter_kinds[index])) {
            throw DexVmError(
                DexVmErrorReason::internal_invariant,
                "intrinsic argument kind mismatch at " +
                    std::to_string(index) + ": " + name + descriptor);
        }
    }
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
    [[maybe_unused]] const auto roots =
        owner->ProtectIntrinsicCall(receiver, arguments);
    auto result = handler(context);
    if (result.kind != vm_kind(method.shape.return_kind)) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "intrinsic return kind mismatch: " + name +
                             descriptor);
    }
    return result;
}

VmMethodId Interpreter::Impl::SelectInvokeTarget(
    const VmMethodId symbolic_method, const InvokeKind kind,
    const std::optional<std::uint16_t> vtable_slot,
    const VmObjectRef receiver, const DexClassId current_class) {
    if (kind != InvokeKind::virtual_call &&
        kind != InvokeKind::interface_call &&
        kind != InvokeKind::super_call) {
        return symbolic_method;
    }
    const auto& named = linker->Method(symbolic_method);
    if (kind == InvokeKind::super_call) {
        const auto& current = linker->Class(current_class);
        if (!current.super.has_value()) {
            FailCode("invoke-super without superclass");
        }
        const auto slot = linker->FindVtableIndex(
            *current.super, named.name, named.descriptor);
        if (!slot.has_value()) {
            FailCode("invoke-super dispatch failed for " + named.name);
        }
        return linker->Class(*current.super).vtable[*slot];
    }

    const auto receiver_class = model->ObjectClass(receiver);
    if (!receiver_class.IsValid()) {
        FailCode("receiver class is unknown for " + named.name);
    }
    const auto& vtable = linker->Class(receiver_class).vtable;
    if (kind == InvokeKind::virtual_call && vtable_slot.has_value() &&
        *vtable_slot < vtable.size()) {
        return vtable[*vtable_slot];
    }
    const auto slot = linker->FindVtableIndex(receiver_class, named.name,
                                               named.descriptor);
    if (slot.has_value()) return vtable[*slot];
    if (linker->GapSurveyEnabled() &&
        linker->Class(receiver_class).is_intrinsic) {
        const auto name = named.name;
        const auto descriptor = named.descriptor;
        return linker->SynthesizeSurveyMethod(receiver_class, name, descriptor,
                                              kind);
    }
    FailCode("virtual dispatch failed for " + named.name + " on " +
             linker->Class(receiver_class).descriptor);
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
            if (thrown.existing.IsValid()) owner->SetPendingException(thrown.existing);
            else ThrowJava(thrown.descriptor, thrown.message);
        } catch (const std::bad_alloc&) {
            model->SetEmergencyReserve(true);
            ThrowJava("Ljava/lang/OutOfMemoryError;",
                      "host allocation failed inside DexVM heap");
            model->SetEmergencyReserve(false);
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
                    error, *linker, *owner, execution, threads);
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
            const auto handler_class = linker->ResolveTypeIndex(
                *method.dex_unit, handler.type_index);
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
                outcome.exception_message = (owner->ThrowableMessage(pending_exception).IsValid() ? owner->StringUtf8(owner->ThrowableMessage(pending_exception)) : std::string{});
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

    impl_->nio.SetObjectModel(&model);
    RegisterIntrinsicStateTable({
        "throwable",
        {},
        [state = impl_.get()](const VmObjectRef owner) {
            state->throwables.erase(owner.Value());
        },
        {}});
    RegisterIntrinsicStateTable({
        "io",
        {}, // File resources and decoders contain no Java references.
        [state = impl_.get()](const VmObjectRef owner) {
            state->io.Sweep(owner);
        },
        [](const VmObjectRef, const VmObjectRef) {}});
    RegisterIntrinsicStateTable({
        "network",
        [state = impl_.get()](const VmObjectRef owner,
                              const VmRootVisitor& visit) {
            state->network.Trace(owner, visit);
        },
        [state = impl_.get()](const VmObjectRef owner) {
            state->network.Sweep(owner);
        },
        {}});
    RegisterIntrinsicStateTable({
        "zip", {},
        [state = impl_.get()](const VmObjectRef owner) {
            state->zip.Sweep(owner);
        },
        [](const VmObjectRef, const VmObjectRef) {}});
    RegisterIntrinsicStateTable({
        "nio",
        [state = impl_.get(), &model](const VmObjectRef owner,
                                      const VmRootVisitor& visit) {
            state->nio_runtime->Trace(model.ToIdentity(owner), visit);
        },
        [state = impl_.get(), &model](const VmObjectRef owner) {
            state->nio_runtime->Sweep(model.ToIdentity(owner));
        },
        [state = impl_.get(), &model](const VmObjectRef source,
                                      const VmObjectRef clone) {
            const auto source_id = model.ToIdentity(source);
            if (state->nio_runtime->Contains(source_id)) {
                state->nio_runtime->Duplicate(model.ToIdentity(clone), source_id, false);
            }
        }});
    RegisterIntrinsicStateTable({"native-bignums", {},
        [state = impl_.get(), &model](VmObjectRef owner) {
            const auto type = state->linker->FindClass("Ljava/math/BigInt;");
            if (!type || model.ObjectClass(owner) != *type) return;
            const auto field = state->linker->FindFieldRecursive(*type, "bignum", "J");
            if (!field) return;
            const auto slot = state->linker->Field(*field).slot;
            const auto slots = model.InstanceSlots(owner);
            const auto token = static_cast<std::uint64_t>(slots[slot].bits) | (static_cast<std::uint64_t>(slots[slot + 1].bits) << 32U);
            state->big_ints.Sweep(token);
        }, {}});
    RegisterIntrinsicStateTable({"guest-native-resources", {},
        [state = impl_.get()](VmObjectRef owner) {
            const auto found = state->guest_native_resources.find(owner.Value());
            if (found == state->guest_native_resources.end()) return;
            state->pending_guest_cleanup.push_back(found->second);
            state->guest_native_resources.erase(found);
        }, {}});
    if (const auto decimal = linker.FindClass(
            "Llibcore/icu/NativeDecimalFormat;"); decimal.has_value()) {
        const auto address = linker.FindFieldRecursive(*decimal, "address", "J");
        const auto close = linker.FindDirectMethod(*decimal, "close", "(J)V");
        if (address.has_value() && close.has_value())
            TrackGuestNativeResourceField(*address, *close);
    }
    const auto track_native_field = [&](const char* owner, const char* field_name,
                                        const char* cleanup_name) {
        const auto type = linker.FindClass(owner);
        const auto native = linker.FindClass("Lcom/android/org/conscrypt/NativeCrypto;");
        if (!type || !native) return;
        linker.EnsureClassLinked(*type);
        linker.EnsureClassLinked(*native);
        const auto field = linker.FindFieldRecursive(*type, field_name, "J");
        const auto cleanup = linker.FindDirectMethod(*native, cleanup_name, "(J)V");
        if (!field || !cleanup)
            throw DexVmError(DexVmErrorReason::unresolved_reference,
                             std::string{"crypto resource metadata: "} + owner +
                                 (!field ? " field" : " cleanup"));
        TrackGuestNativeResourceField(*field, *cleanup);
    };
    track_native_field("Lcom/android/org/conscrypt/OpenSSLMessageDigestJDK;", "ctx",
                       "EVP_MD_CTX_destroy");
    track_native_field("Lcom/android/org/conscrypt/OpenSSLDigestContext;", "context",
                       "EVP_MD_CTX_destroy");
    track_native_field("Lcom/android/org/conscrypt/OpenSSLKey;", "ctx", "EVP_PKEY_free");
    if (const auto type = linker.FindClass("Lorg/ogplay/security/BksPrivateKey;");
        type.has_value()) {
        const auto native = linker.FindClass("Lorg/ogplay/security/NativeKeyStoreCrypto;");
        linker.EnsureClassLinked(*type);
        linker.EnsureClassLinked(*native);
        const auto field = linker.FindFieldRecursive(*type, "token", "J");
        const auto cleanup = linker.FindDirectMethod(*native, "freePrivateKey", "(J)V");
        if (!field || !cleanup)
            throw DexVmError(DexVmErrorReason::unresolved_reference,
                             "BKS private-key resource metadata");
        TrackGuestNativeResourceField(*field, *cleanup);
    }
    const auto track_tls_field = [&](const char* owner, const char* field_name,
                                     const char* cleanup_name) {
        const auto type = linker.FindClass(owner);
        const auto native = linker.FindClass("Lorg/ogplay/security/NativeTls;");
        if (!type || !native) return;
        linker.EnsureClassLinked(*type);
        linker.EnsureClassLinked(*native);
        const auto field = linker.FindFieldRecursive(*type, field_name, "J");
        const auto cleanup = linker.FindDirectMethod(*native, cleanup_name, "(J)V");
        if (!field || !cleanup)
            throw DexVmError(DexVmErrorReason::unresolved_reference,
                             std::string{"TLS resource metadata: "} + owner);
        TrackGuestNativeResourceField(*field, *cleanup);
    };
    track_tls_field("Lorg/ogplay/security/OgPlaySslSocket;", "ssl", "freeSsl");
    track_tls_field("Lorg/ogplay/security/OgPlaySslContextSpi;", "nativeContext",
                    "freeContext");
    const auto string_class = linker.FindClass("Ljava/lang/String;");
    const auto class_class = linker.FindClass("Ljava/lang/Class;");
    if (string_class.has_value() && class_class.has_value()) {
        model.SetCoreClasses(*string_class, *class_class);
    }
    impl_->class_loaders =
        std::make_unique<ClassLoaderFacade>(linker, model);
    impl_->reflection =
        std::make_unique<ReflectionRuntime>(*this, linker, model);
    impl_->unsafe = std::make_unique<UnsafeRuntime>(*this);
}

BigIntRuntime& Interpreter::BigInts() { return impl_->big_ints; }

IoRuntime& Interpreter::IO() { return impl_->io; }

const IoRuntime& Interpreter::IO() const { return impl_->io; }

NetworkRuntime& Interpreter::Network() { return impl_->network; }

const NetworkRuntime& Interpreter::Network() const { return impl_->network; }

NioRuntime& Interpreter::NIO() { return *impl_->nio_runtime; }

const NioRuntime& Interpreter::NIO() const { return *impl_->nio_runtime; }

void Interpreter::SetNioRuntime(NioRuntime* const runtime) noexcept {
    impl_->nio_runtime = runtime == nullptr ? &impl_->nio : runtime;
    impl_->nio_runtime->SetObjectModel(impl_->model);
}

ZipRuntime& Interpreter::ZIP() { return impl_->zip; }

const ZipRuntime& Interpreter::ZIP() const { return impl_->zip; }

Interpreter::~Interpreter() {
    if (impl_->nio_runtime != &impl_->nio) {
        impl_->nio_runtime->SweepDomain(JniObjectDomain::dex_vm);
        impl_->nio_runtime->SetObjectModel(nullptr);
    }
}

ClassLoaderFacade& Interpreter::ClassLoaders() noexcept {
    return *impl_->class_loaders;
}

UnsafeRuntime& Interpreter::Unsafe() noexcept {
    return *impl_->unsafe;
}

ReflectionRuntime& Interpreter::Reflection() noexcept {
    return *impl_->reflection;
}

VmCallOutcome Interpreter::Call(const VmMethodId method_id,
                                const std::span<const VmValue> arguments) {
    VmExecutionLockScope lock_scope(impl_->execution_lock);
    if (ExitCode().has_value()) {
        throw DexVmError(DexVmErrorReason::thread_stopped,
                         "guest VM has exited");
    }
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
                if (thrown.existing.IsValid()) SetPendingException(thrown.existing);
                else impl_->ThrowJava(thrown.descriptor, thrown.message);
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
                    outcome.exception_message = (ThrowableMessage(pending_exception).IsValid() ? StringUtf8(ThrowableMessage(pending_exception)) : std::string{});
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
            outcome.exception_message = (ThrowableMessage(pending_exception).IsValid() ? StringUtf8(ThrowableMessage(pending_exception)) : std::string{});
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
    auto& constructing = impl_->Execution().constructing_throwable;
    if (constructing) throw DexVmError(DexVmErrorReason::internal_invariant,
                                       "recursive failure while constructing VM exception: " + std::string(descriptor));
    struct ConstructionScope {
        bool& active;
        JavaObjectModel& model;
        bool previous_reserve;
        ConstructionScope(bool& value, JavaObjectModel& heap)
            : active(value), model(heap), previous_reserve(heap.SetEmergencyReserve(true)) { active = true; }
        ~ConstructionScope() { active = false; model.SetEmergencyReserve(previous_reserve); }
    } scope(constructing, *impl_->model);
    const auto java_class = impl_->linker->FindClass(descriptor);
    if (!java_class.has_value()) {
        throw DexVmError(DexVmErrorReason::unknown_class,
                         "throwable class is not registered: " +
                             std::string(descriptor));
    }
    const auto throwable = impl_->AllocateInstance(*java_class);
    const auto root = ProtectReferences(std::array{throwable});
    const auto stack = impl_->CaptureStack();
    InitializeThrowable(throwable, message.empty() ? VmObjectRef{} : NewStringUtf8(message));
    impl_->throwables[throwable.Value()].stack = stack;
    return throwable;
}

void Interpreter::SetPendingException(const VmObjectRef throwable) {
    impl_->SetPendingExisting(throwable);
}

namespace {
Slot& ThrowableField(Interpreter& vm, const VmObjectRef throwable,
                     const char* name, const char* descriptor) {
    const auto type = vm.Linker().ResolveDescriptor("Ljava/lang/Throwable;");
    const auto field = vm.Linker().FindFieldRecursive(type, name, descriptor);
    if (!field) throw DexVmError(DexVmErrorReason::internal_invariant,
                                 std::string("missing Throwable field: ") + name);
    if (!vm.Linker().IsAssignable(type, vm.Model().ObjectClass(throwable)))
        throw DexVmError(DexVmErrorReason::internal_invariant, "Throwable field receiver has wrong type");
    auto slots = vm.Model().InstanceSlots(throwable);
    const auto slot = vm.Linker().Field(*field).slot;
    if (slot >= slots.size())
        throw DexVmError(DexVmErrorReason::internal_invariant, "Throwable field slot is out of range");
    return slots[slot];
}
}

void Interpreter::InitializeThrowable(const VmObjectRef throwable, const VmObjectRef message) {
    const auto root = ProtectReferences(std::array{throwable, message});
    const auto ctor = impl_->linker->FindDirectMethod(
        impl_->linker->ResolveDescriptor("Ljava/lang/Throwable;"), "<init>", "(Ljava/lang/String;)V");
    if (!ctor) throw DexVmError(DexVmErrorReason::internal_invariant, "missing BootDex Throwable constructor");
    const auto result = Call(*ctor, std::array{VmValue::Ref(throwable), VmValue::Ref(message)});
    if (result.exception.IsValid()) throw VmJavaThrow{
        impl_->linker->Class(result.exception_class).descriptor, result.exception_message, result.exception};
}

void Interpreter::InitThrowableCause(VmObjectRef throwable, VmObjectRef cause) {
    auto& field = ThrowableField(*this, throwable, "cause", "Ljava/lang/Throwable;");
    if (VmObjectRef(field.bits) != throwable)
        throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "cause already initialized"};
    if (throwable == cause) throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "self-causation not permitted"};
    field = {cause.Value(), SlotTag::ref};
}
VmObjectRef Interpreter::ThrowableCause(VmObjectRef throwable) const {
    const auto cause = VmObjectRef(ThrowableField(*impl_->owner, throwable, "cause", "Ljava/lang/Throwable;").bits);
    return cause == throwable ? VmObjectRef{} : cause;
}

void Interpreter::SetThrowableMessage(const VmObjectRef throwable, const VmObjectRef message) {
    ThrowableField(*this, throwable, "detailMessage", "Ljava/lang/String;") = {message.Value(), SlotTag::ref};
}

VmObjectRef Interpreter::ThrowableMessage(const VmObjectRef throwable) const {
    return VmObjectRef(ThrowableField(*impl_->owner, throwable, "detailMessage", "Ljava/lang/String;").bits);
}

VmObjectRef Interpreter::CaptureThrowableStack() {
    std::vector<std::pair<VmMethodId, std::uint32_t>> frames;
    bool trim = true;
    const auto type = impl_->linker->ResolveDescriptor("Ljava/lang/Throwable;");
    for (auto it = impl_->Execution().frames.rbegin(); it != impl_->Execution().frames.rend(); ++it) {
        // AOSP dvmFillInStackTrace removes the leading Throwable implementation frames.
        if (trim && impl_->linker->IsAssignable(type, it->method->owner)) continue;
        trim = false;
        frames.emplace_back(it->method->id, it->pc);
    }
    const auto array = impl_->model->NewPrimitiveArray(
        impl_->linker->ResolveDescriptor("[I"), JniPrimitiveKind::integer,
        static_cast<JniSize>(frames.size() * 2));
    for (std::size_t i = 0; i < frames.size(); ++i) {
        impl_->model->SetPrimitiveElement(array, static_cast<JniSize>(i * 2), frames[i].first.Value());
        impl_->model->SetPrimitiveElement(array, static_cast<JniSize>(i * 2 + 1), frames[i].second);
    }
    return array;
}

VmObjectRef Interpreter::MaterializeThrowableStack(const VmObjectRef snapshot) {
    if (!snapshot.IsValid()) return VmObjectRef{}; // AOSP nativeGetStackTrace(null).
    if (impl_->model->ObjectClass(snapshot) != impl_->linker->ResolveDescriptor("[I") ||
        (impl_->model->ArrayLength(snapshot) % 2) != 0)
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid Throwable stack snapshot"};
    const auto count = impl_->model->ArrayLength(snapshot) / 2;
    const auto type = impl_->linker->ResolveDescriptor("Ljava/lang/StackTraceElement;");
    const auto array = impl_->model->NewObjectArray(
        impl_->linker->ResolveDescriptor("[Ljava/lang/StackTraceElement;"), type, count);
    const auto roots = ProtectReferences(std::array{snapshot, array});
    for (JniSize i = 0; i < count; ++i) {
        const auto method_id = VmMethodId(static_cast<std::uint32_t>(impl_->model->GetPrimitiveElement(snapshot, i * 2)));
        const auto& method = [&]() -> const LinkedMethod& {
            try { return impl_->linker->Method(method_id); }
            catch (const DexVmError& error) {
                if (error.Reason() != DexVmErrorReason::invalid_member) throw;
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid method in Throwable stack snapshot"};
            }
        }();
        const auto element = NewIntrinsicInstance("Ljava/lang/StackTraceElement;");
        impl_->model->SetObjectElement(array, i, element);
        const auto set = [&](const char* name, const char* descriptor, Slot value) {
            const auto field = impl_->linker->FindFieldRecursive(type, name, descriptor);
            if (!field) throw DexVmError(DexVmErrorReason::internal_invariant, "missing StackTraceElement field");
            impl_->model->InstanceSlots(element)[impl_->linker->Field(*field).slot] = value;
        };
        auto declaring_class = impl_->linker->Class(method.owner).descriptor;
        declaring_class = declaring_class.substr(1, declaring_class.size() - 2);
        std::replace(declaring_class.begin(), declaring_class.end(), '/', '.');
        set("declaringClass", "Ljava/lang/String;", {NewStringUtf8(declaring_class).Value(), SlotTag::ref});
        set("methodName", "Ljava/lang/String;", {NewStringUtf8(method.name).Value(), SlotTag::ref});
        set("fileName", "Ljava/lang/String;", {0, SlotTag::ref});
        set("lineNumber", "I", {static_cast<std::uint32_t>(method.kind == MethodKind::native ? -2 : -1), SlotTag::cat1});
    }
    return array;
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
