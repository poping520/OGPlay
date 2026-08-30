    const auto& invoke = code.invokes[instruction.invoke];
    if (instruction.handler == FastHandler::invoke_checked) {
        const auto invoke_kind =
            invoke.base_opcode == 0x6eU ? InvokeKind::virtual_call
            : invoke.base_opcode == 0x6fU ? InvokeKind::super_call
            : invoke.base_opcode == 0x70U ? InvokeKind::private_direct
            : invoke.base_opcode == 0x71U ? InvokeKind::static_call
                                          : InvokeKind::interface_call;
        const auto resolved = linker->ResolveMethodIndex(
            static_cast<std::uint32_t>(instruction.extra), invoke_kind);
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
        std::size_t pair_cursor = named.is_static ? 0U : 1U;
        for (const auto kind : invoke.argument_shorty) {
            if (kind == 'J' || kind == 'D') {
                const auto lo =
                    pair_cursor < invoke.registers.size()
                        ? static_cast<std::uint32_t>(
                              invoke.registers[pair_cursor])
                        : 0U;
                const auto listed_hi =
                    pair_cursor + 1U < invoke.registers.size()
                        ? std::optional<std::uint32_t>(
                              invoke.registers[pair_cursor + 1U])
                        : std::nullopt;
                CheckInvokeWidePair(frame, lo, listed_hi, !invoke.is_range);
                pair_cursor += 2U;
            } else {
                ++pair_cursor;
            }
        }
        invoke.resolved_method = resolved.method.Value();
        invoke.is_static = named.is_static;
        invoke.vtable_index = named.vtable_index;
        instruction.handler = FastHandler::invoke_fast;
    }

    bool continue_same_frame = false;
    {
    VmValue small_storage[8];
    std::vector<VmValue> large_storage;
    VmValue* argument_storage = small_storage;
    if (invoke.registers.size() > 8U) {
        large_storage.resize(invoke.registers.size());
        argument_storage = large_storage.data();
    }
    std::size_t argument_count{};
    std::size_t register_cursor{};
    VmObjectRef receiver;
    if (!invoke.is_static) {
        receiver = GetFastRef(regs, invoke.registers[register_cursor]);
        if (!receiver.IsValid()) {
            const auto named_id = VmMethodId(invoke.resolved_method);
            const auto& named = linker->Method(named_id);
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
    const std::span<const VmValue> arguments(argument_storage,
                                             argument_count);

    const auto named_id = VmMethodId(invoke.resolved_method);
    const auto kind =
        invoke.base_opcode == 0x6eU ? InvokeKind::virtual_call
        : invoke.base_opcode == 0x6fU ? InvokeKind::super_call
        : invoke.base_opcode == 0x70U ? InvokeKind::private_direct
        : invoke.base_opcode == 0x71U ? InvokeKind::static_call
                                      : InvokeKind::interface_call;
    const auto slot = invoke.vtable_index >= 0
                          ? std::optional<std::uint16_t>(
                                static_cast<std::uint16_t>(invoke.vtable_index))
                          : std::nullopt;
    const auto target_id = SelectInvokeTarget(
        named_id, kind, slot, receiver, frame.method->owner);
    const auto callee_static = linker->Method(target_id).is_static;
    const auto callee_kind = linker->Method(target_id).kind;
    const auto callee_owner = linker->Method(target_id).owner;
    if (callee_static) {
        OGPLAY_SYNC_EXECUTION();
        EnsureInitialized(execution, callee_owner);
        ticks = execution.ticks;
        if (pending_exception.IsValid()) goto yield;
    }

    switch (callee_kind) {
        case MethodKind::interpreted:
            linker->PrecheckMethod(target_id);
            frame_ptr->fast_ip = ip + 1U;
            OGPLAY_SYNC_EXECUTION();
            PushInterpretedFrame(execution, linker->Method(target_id),
                                 arguments, instruction.width);
            goto yield;
        case MethodKind::intrinsic: {
            const auto& method = linker->Method(target_id);
            const auto rest =
                method.is_static ? arguments : arguments.subspan(1);
            const MethodMonitorScope monitor(*this, method, arguments);
            OGPLAY_RECORD_TRACE(DexVmTraceKind::method_enter, execution,
                                &method, frame.pc);
            OGPLAY_SYNC_EXECUTION();
            try {
                frame.last_result = InvokeIntrinsic(
                    linker->Method(target_id), receiver, rest);
                ticks = execution.ticks;
                OGPLAY_RECORD_TRACE(DexVmTraceKind::method_exit, execution,
                                    &linker->Method(target_id), frame.pc);
            } catch (const VmJavaThrow& thrown) {
                ticks = execution.ticks;
                ThrowJava(thrown.descriptor, thrown.message);
                OGPLAY_RECORD_TRACE(DexVmTraceKind::method_exit, execution,
                                    nullptr, frame.pc, 0, 1);
                goto yield;
            } catch (...) {
                ticks = execution.ticks;
                OGPLAY_RECORD_TRACE(DexVmTraceKind::method_exit, execution,
                                    nullptr, frame.pc, 0, 1);
                throw;
            }
            if (pending_exception.IsValid()) goto yield;
            continue_same_frame = true;
            break;
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
            OGPLAY_RECORD_TRACE(DexVmTraceKind::native_enter, execution,
                                &method, frame.pc);
            OGPLAY_RECORD_TRACE(DexVmTraceKind::method_enter, execution,
                                &method, frame.pc);
            OGPLAY_SYNC_EXECUTION();
            try {
                frame.last_result =
                    bridge->Invoke(linker->Method(target_id), receiver, rest);
                ticks = execution.ticks;
                OGPLAY_RECORD_TRACE(DexVmTraceKind::native_exit, execution,
                                    &linker->Method(target_id), frame.pc);
                OGPLAY_RECORD_TRACE(DexVmTraceKind::method_exit, execution,
                                    &linker->Method(target_id), frame.pc);
            } catch (...) {
                ticks = execution.ticks;
                OGPLAY_RECORD_TRACE(DexVmTraceKind::native_exit, execution,
                                    nullptr, frame.pc, 0, 1);
                OGPLAY_RECORD_TRACE(DexVmTraceKind::method_exit, execution,
                                    nullptr, frame.pc, 0, 1);
                throw;
            }
            if (pending_exception.IsValid()) goto yield;
            continue_same_frame = true;
            break;
        }
        case MethodKind::abstract:
            FailCode("abstract method invoked: " +
                     linker->Method(target_id).name);
    }
    }
    if (continue_same_frame) {
        OGPLAY_DISPATCH_NEXT();
    }
    FailCode("unreachable invoke method kind");
