// Direct FastCode invoke handlers. Register word lists are built with
// FastCode; constant-pool resolution and descriptor shorty parsing happen
// once under VmExecutionLock before invoke_checked flips to invoke_fast.

#include "interpreter_internal.h"

namespace ogplay::runtime::dexvm {

void Interpreter::Impl::StepInvoke(
    InterpreterExecutionState& execution, const FastCode& code,
    const FastInstruction& instruction) {
    auto& frame = execution.frames.back();
    auto& pending_exception = execution.pending_exception;
    Tick(execution);
    ++stats.executed_instructions;
    if (!trace_ring.empty()) {
        RecordTrace(DexVmTraceKind::instruction, execution, frame.method,
                    instruction.dex_pc, instruction.opcode);
    }

    const auto& invoke = code.invokes[instruction.invoke];
    if (instruction.handler == FastHandler::invoke_checked) {
        const bool direct_or_static = invoke.base_opcode == 0x70U ||
                                      invoke.base_opcode == 0x71U;
        const auto resolved = linker->ResolveMethodIndex(
            static_cast<std::uint32_t>(instruction.extra), direct_or_static);
        const auto& named = linker->Method(resolved.method);
        if (invoke.registers.size() != named.ins_words) {
            FailCode("invoke argument words do not match descriptor of " +
                     named.name + " (" +
                     std::to_string(invoke.registers.size()) + " vs " +
                     std::to_string(named.ins_words) + ")");
        }

        invoke.argument_shorty.clear();
        std::size_t words = named.is_static ? 0U : 1U;
        std::size_t cursor = named.descriptor.find('(') + 1U;
        while (named.descriptor[cursor] != ')') {
            const auto kind = named.descriptor[cursor];
            if (kind == 'J' || kind == 'D') {
                invoke.argument_shorty.push_back(kind);
                words += 2U;
                ++cursor;
            } else if (kind == 'L' || kind == '[') {
                invoke.argument_shorty.push_back('L');
                ++words;
                while (named.descriptor[cursor] == '[') ++cursor;
                if (named.descriptor[cursor] == 'L') {
                    cursor = named.descriptor.find(';', cursor) + 1U;
                } else {
                    ++cursor;
                }
            } else {
                invoke.argument_shorty.push_back(kind);
                ++words;
                ++cursor;
            }
        }
        if (words != invoke.registers.size()) {
            FailCode("invoke shorty words do not match register list of " +
                     named.name);
        }
        invoke.resolved_method = resolved.method.Value();
        instruction.handler = FastHandler::invoke_fast;
    }

    const auto named_id = VmMethodId(invoke.resolved_method);
    const auto& named = linker->Method(named_id);
    std::vector<VmValue> arguments;
    arguments.reserve(invoke.argument_shorty.size() +
                      (named.is_static ? 0U : 1U));
    std::size_t register_cursor{};
    VmObjectRef receiver;
    if (!named.is_static) {
        receiver = GetFastRef(frame, invoke.registers[register_cursor]);
        if (!receiver.IsValid()) {
            ThrowJava(
                "Ljava/lang/NullPointerException;",
                "invoke on null receiver: " +
                    linker->Class(named.owner).descriptor + "." + named.name +
                    named.descriptor + " from v" +
                    std::to_string(invoke.registers[register_cursor]) +
                    " called by " +
                    linker->Class(frame.method->owner).descriptor + "." +
                    frame.method->name + " pc " +
                    std::to_string(frame.pc));
            return;
        }
        arguments.push_back(VmValue::Ref(receiver));
        ++register_cursor;
    }
    for (const auto kind : invoke.argument_shorty) {
        if (kind == 'J' || kind == 'D') {
            arguments.push_back(VmValue::Long(static_cast<std::int64_t>(
                GetFastWide(frame, invoke.registers[register_cursor]))));
            register_cursor += 2U;
        } else if (kind == 'L') {
            arguments.push_back(VmValue::Ref(
                GetFastRef(frame, invoke.registers[register_cursor])));
            ++register_cursor;
        } else {
            arguments.push_back(VmValue::Int(static_cast<std::int32_t>(
                GetFastCat1(frame, invoke.registers[register_cursor]))));
            ++register_cursor;
        }
    }

    VmMethodId target = named_id;
    if (invoke.base_opcode == 0x6eU || invoke.base_opcode == 0x72U) {
        const auto receiver_class = model->ObjectClass(receiver);
        if (!receiver_class.IsValid()) {
            FailCode("receiver class is unknown for " + named.name);
        }
        const auto index = linker->FindVtableIndex(
            receiver_class, named.name, named.descriptor);
        if (index.has_value()) {
            target = linker->Class(receiver_class).vtable[*index];
        } else if (linker->GapSurveyEnabled() &&
                   linker->Class(receiver_class).is_intrinsic) {
            const auto name = named.name;
            const auto descriptor = named.descriptor;
            target = linker->SynthesizeSurveyMethod(receiver_class, name,
                                                    descriptor, false);
        } else {
            FailCode("virtual dispatch failed for " + named.name + " on " +
                     linker->Class(receiver_class).descriptor);
        }
    } else if (invoke.base_opcode == 0x6fU) {
        const auto& current = linker->Class(frame.method->owner);
        if (!current.super.has_value()) {
            FailCode("invoke-super without superclass");
        }
        const auto index = linker->FindVtableIndex(
            *current.super, named.name, named.descriptor);
        if (!index.has_value()) {
            FailCode("invoke-super dispatch failed for " + named.name);
        }
        target = linker->Class(*current.super).vtable[*index];
    }
    const auto& method = linker->Method(target);
    if (method.is_static) {
        EnsureInitialized(execution, method.owner);
        if (pending_exception.IsValid()) return;
    }

    switch (method.kind) {
        case MethodKind::interpreted:
            linker->PrecheckMethod(target);
            PushInterpretedFrame(execution, method, arguments,
                                 instruction.width);
            return;
        case MethodKind::intrinsic: {
            const auto rest =
                method.is_static
                    ? std::span<const VmValue>(arguments)
                    : std::span<const VmValue>(arguments).subspan(1);
            const MethodMonitorScope monitor(*this, method, arguments);
            RecordTrace(DexVmTraceKind::method_enter, execution, &method,
                        frame.pc);
            try {
                frame.last_result = InvokeIntrinsic(method, receiver, rest);
                RecordTrace(DexVmTraceKind::method_exit, execution, &method,
                            frame.pc);
            } catch (const VmJavaThrow& thrown) {
                ThrowJava(thrown.descriptor, thrown.message);
                RecordTrace(DexVmTraceKind::method_exit, execution, &method,
                            frame.pc, 0, 1);
                return;
            } catch (...) {
                RecordTrace(DexVmTraceKind::method_exit, execution, &method,
                            frame.pc, 0, 1);
                throw;
            }
            if (!pending_exception.IsValid()) frame.pc += instruction.width;
            return;
        }
        case MethodKind::native: {
            if (bridge == nullptr) {
                if (ledger != nullptr) {
                    ledger->RecordUnimplemented("dexvm.native_bridge", 0);
                }
                throw DexVmError(
                    DexVmErrorReason::native_bridge_unavailable,
                    "native method requires the JNI bridge: " +
                        linker->Class(method.owner).descriptor + "." +
                        method.name + method.descriptor);
            }
            const auto rest =
                method.is_static
                    ? std::span<const VmValue>(arguments)
                    : std::span<const VmValue>(arguments).subspan(1);
            ++stats.native_calls;
            const MethodMonitorScope monitor(*this, method, arguments);
            const NativeFrame native_frame(*this);
            RecordTrace(DexVmTraceKind::native_enter, execution, &method,
                        frame.pc);
            RecordTrace(DexVmTraceKind::method_enter, execution, &method,
                        frame.pc);
            try {
                frame.last_result = bridge->Invoke(method, receiver, rest);
                RecordTrace(DexVmTraceKind::native_exit, execution, &method,
                            frame.pc);
                RecordTrace(DexVmTraceKind::method_exit, execution, &method,
                            frame.pc);
            } catch (...) {
                RecordTrace(DexVmTraceKind::native_exit, execution, &method,
                            frame.pc, 0, 1);
                RecordTrace(DexVmTraceKind::method_exit, execution, &method,
                            frame.pc, 0, 1);
                throw;
            }
            if (!pending_exception.IsValid()) frame.pc += instruction.width;
            return;
        }
        case MethodKind::abstract:
            throw VmJavaThrow{"Ljava/lang/AbstractMethodError;",
                              linker->Class(method.owner).descriptor + "." +
                                  method.name + method.descriptor};
    }
    FailCode("unreachable invoke method kind");
}

}  // namespace ogplay::runtime::dexvm
