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
                ++cursor;
                ++words;
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
    std::array<VmValue, 256> argument_storage{};
    std::size_t argument_count{};
    std::size_t register_cursor{};
    VmObjectRef receiver;
    if (!named.is_static) {
        receiver = GetFastRef(regs, invoke.registers[register_cursor]);
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
            goto yield;
        }
        argument_storage[argument_count++] = VmValue::Ref(receiver);
        ++register_cursor;
    }
    for (const auto kind : invoke.argument_shorty) {
        if (kind == 'J' || kind == 'D') {
            argument_storage[argument_count++] = VmValue::Long(
                static_cast<std::int64_t>(
                    GetFastWide(regs, invoke.registers[register_cursor])));
            register_cursor += 2U;
        } else if (kind == 'L') {
            argument_storage[argument_count++] = VmValue::Ref(
                GetFastRef(regs, invoke.registers[register_cursor]));
            ++register_cursor;
        } else {
            argument_storage[argument_count++] = VmValue::Int(
                static_cast<std::int32_t>(
                    GetFastCat1(regs, invoke.registers[register_cursor])));
            ++register_cursor;
        }
    }
    const std::span<const VmValue> arguments(argument_storage.data(),
                                             argument_count);

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
    const auto target_id = target;
    const auto callee_static = linker->Method(target_id).is_static;
    const auto callee_kind = linker->Method(target_id).kind;
    const auto callee_owner = linker->Method(target_id).owner;
    if (callee_static) {
        stats.executed_instructions += executed;
        executed = 0;
        EnsureInitialized(execution, callee_owner);
        ticks = execution.ticks;
        if (pending_exception.IsValid()) goto yield;
    }

    switch (callee_kind) {
        case MethodKind::interpreted:
            linker->PrecheckMethod(target_id);
            PushInterpretedFrame(execution, linker->Method(target_id),
                                 arguments, instruction.width);
            goto yield;
        case MethodKind::intrinsic: {
            const auto& method = linker->Method(target_id);
            const auto rest =
                method.is_static ? arguments : arguments.subspan(1);
            const MethodMonitorScope monitor(*this, method, arguments);
            RecordTrace(DexVmTraceKind::method_enter, execution, &method,
                        frame.pc);
            stats.executed_instructions += executed;
            executed = 0;
            try {
                frame.last_result = InvokeIntrinsic(
                    linker->Method(target_id), receiver, rest);
                ticks = execution.ticks;
                RecordTrace(DexVmTraceKind::method_exit, execution,
                            &linker->Method(target_id), frame.pc);
            } catch (const VmJavaThrow& thrown) {
                ticks = execution.ticks;
                ThrowJava(thrown.descriptor, thrown.message);
                RecordTrace(DexVmTraceKind::method_exit, execution, nullptr,
                            frame.pc, 0, 1);
                goto yield;
            } catch (...) {
                ticks = execution.ticks;
                RecordTrace(DexVmTraceKind::method_exit, execution, nullptr,
                            frame.pc, 0, 1);
                throw;
            }
            if (pending_exception.IsValid()) goto yield;
            ip += 1U;
            goto fetch;
        }
        case MethodKind::native: {
            const auto& method = linker->Method(target_id);
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
                method.is_static ? arguments : arguments.subspan(1);
            ++stats.native_calls;
            const MethodMonitorScope monitor(*this, method, arguments);
            const NativeFrame native_frame(*this);
            RecordTrace(DexVmTraceKind::native_enter, execution, &method,
                        frame.pc);
            RecordTrace(DexVmTraceKind::method_enter, execution, &method,
                        frame.pc);
            stats.executed_instructions += executed;
            executed = 0;
            try {
                frame.last_result =
                    bridge->Invoke(linker->Method(target_id), receiver, rest);
                ticks = execution.ticks;
                RecordTrace(DexVmTraceKind::native_exit, execution,
                            &linker->Method(target_id), frame.pc);
                RecordTrace(DexVmTraceKind::method_exit, execution,
                            &linker->Method(target_id), frame.pc);
            } catch (...) {
                ticks = execution.ticks;
                RecordTrace(DexVmTraceKind::native_exit, execution, nullptr,
                            frame.pc, 0, 1);
                RecordTrace(DexVmTraceKind::method_exit, execution, nullptr,
                            frame.pc, 0, 1);
                throw;
            }
            if (pending_exception.IsValid()) goto yield;
            ip += 1U;
            goto fetch;
        }
        case MethodKind::abstract:
            FailCode("abstract method invoked: " +
                     linker->Method(target_id).name);
    }
    FailCode("unreachable invoke method kind");
