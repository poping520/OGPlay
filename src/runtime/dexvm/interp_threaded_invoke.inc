const auto& invoke = code.invokes[instruction.invoke];
if (instruction.handler == FastHandler::invoke_checked) {
    const auto invoke_kind = invoke.base_opcode == 0x6eU   ? InvokeKind::virtual_call
                             : invoke.base_opcode == 0x6fU ? InvokeKind::super_call
                             : invoke.base_opcode == 0x70U ? InvokeKind::private_direct
                             : invoke.base_opcode == 0x71U ? InvokeKind::static_call
                                                           : InvokeKind::interface_call;
    const auto resolved = linker->ResolveMethodIndex(dex_unit, static_cast<std::uint32_t>(instruction.extra),
                                                     invoke_kind, frame.method->owner);
    const auto& named = linker->Method(resolved.method);
    if (invoke.registers.size() != named.ins_words) {
        FailCode("invoke argument words do not match descriptor of " + named.name + " (" +
                 std::to_string(invoke.registers.size()) + " vs " + std::to_string(named.ins_words) + ")");
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
            while (named.descriptor[cursor] == '[')
                ++cursor;
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
        FailCode("invoke shorty words do not match register list of " + named.name);
    }
    std::size_t pair_cursor = named.is_static ? 0U : 1U;
    for (const auto kind : invoke.argument_shorty) {
        if (kind == 'J' || kind == 'D') {
            const auto lo =
                pair_cursor < invoke.registers.size() ? static_cast<std::uint32_t>(invoke.registers[pair_cursor]) : 0U;
            const auto listed_hi = pair_cursor + 1U < invoke.registers.size()
                                       ? std::optional<std::uint32_t>(invoke.registers[pair_cursor + 1U])
                                       : std::nullopt;
            CheckInvokeWidePair(frame, lo, listed_hi, !invoke.is_range);
            pair_cursor += 2U;
        } else {
            ++pair_cursor;
        }
    }
    invoke.resolved_method = resolved.method.Value();
    invoke.is_static = named.is_static;
    if (invoke_kind == InvokeKind::virtual_call || invoke_kind == InvokeKind::private_direct)
        invoke.fast_op = named.fast_op;
    if (invoke_kind == InvokeKind::private_direct ||
        (invoke_kind == InvokeKind::virtual_call &&
         (!named.overridable || (linker->Class(named.owner).access_flags & kAccFinal))))
        invoke.nio_fast_op = named.nio_fast_op;
    invoke.vtable_index = named.vtable_index;
    instruction.handler = FastHandler::invoke_fast;
}

if (invoke.fast_op != StringFastOp::none && trace_ring.empty() &&
    TryStringFastOp(invoke.fast_op, *model, invoke, regs, frame.last_result)) {
    execution.exit_result = VmValue::Void();
    OGPLAY_DISPATCH_NEXT();
}

if (invoke.nio_fast_op != 0 && !config.disable_nio_fast_ops && trace_ring.empty() &&
    TryNioFastOp(*owner, invoke.nio_fast_op, invoke, regs, frame.last_result)) {
    execution.exit_result = VmValue::Void();
    OGPLAY_DISPATCH_NEXT();
}
bool continue_same_frame = false;
{
    // A new invoke invalidates the previous result before any nested GC.
    frame.last_result = VmValue::Void();
    execution.exit_result = VmValue::Void();
    std::size_t argument_count{};
    std::size_t register_cursor{};
    VmObjectRef receiver;
    if (!invoke.is_static) {
        receiver = GetFastRef(regs, invoke.registers[register_cursor]);
        if (!receiver.IsValid()) {
            const auto named_id = VmMethodId(invoke.resolved_method);
            const auto& named = linker->Method(named_id);
            ThrowJava("Ljava/lang/NullPointerException;",
                      "invoke on null receiver: " + linker->Class(named.owner).descriptor + "." + named.name +
                          named.descriptor + " from v" + std::to_string(invoke.registers[register_cursor]) +
                          " called by " + linker->Class(frame.method->owner).descriptor + "." + frame.method->name +
                          " pc " + std::to_string(frame.pc));
            goto yield;
        }
        ++register_cursor;
    }
    const auto named_id = VmMethodId(invoke.resolved_method);
    const auto kind = invoke.base_opcode == 0x6eU   ? InvokeKind::virtual_call
                      : invoke.base_opcode == 0x6fU ? InvokeKind::super_call
                      : invoke.base_opcode == 0x70U ? InvokeKind::private_direct
                      : invoke.base_opcode == 0x71U ? InvokeKind::static_call
                                                    : InvokeKind::interface_call;
    const auto slot = invoke.vtable_index >= 0
                          ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(invoke.vtable_index))
                          : std::nullopt;
    const auto target_id = SelectInvokeTarget(named_id, kind, slot, receiver, frame.method->owner);
    const auto target_fast_op = linker->Method(target_id).fast_op;
    if (invoke.fast_op == StringFastOp::none && target_fast_op != StringFastOp::none && trace_ring.empty() &&
        TryStringFastOp(target_fast_op, *model, invoke, regs, frame.last_result)) {
        OGPLAY_DISPATCH_NEXT();
    }
    const auto nio_op = linker->Method(target_id).nio_fast_op;
    if (invoke.nio_fast_op == 0 && nio_op != 0 && !invoke.is_static && !config.disable_nio_fast_ops &&
        trace_ring.empty() && TryNioFastOp(*owner, nio_op, invoke, regs, frame.last_result)) {
        OGPLAY_DISPATCH_NEXT();
    }
    if (nio_op != 0 && invoke.is_static && !config.disable_nio_fast_ops && trace_ring.empty()) {
        OGPLAY_SYNC_EXECUTION();
        EnsureInitialized(execution, linker->Method(target_id).owner);
        ticks = execution.ticks;
        if (pending_exception.IsValid()) goto yield;
        if (TryNioFastOp(*owner, nio_op, invoke, regs, frame.last_result)) {
            OGPLAY_DISPATCH_NEXT();
        }
    }
    VmValue small_storage[8];
    std::vector<VmValue> large_storage;
    VmValue* argument_storage = small_storage;
    if (invoke.registers.size() > 8U) {
        large_storage.resize(invoke.registers.size());
        argument_storage = large_storage.data();
    }
    if (!invoke.is_static) argument_storage[argument_count++] = VmValue::Ref(receiver);
    for (const auto kind : invoke.argument_shorty) {
        if (kind == 'J' || kind == 'D') {
            argument_storage[argument_count++] =
                VmValue::Long(static_cast<std::int64_t>(GetFastWide(regs, invoke.registers[register_cursor])));
            register_cursor += 2U;
        } else if (kind == 'L') {
            argument_storage[argument_count++] = VmValue::Ref(GetFastRef(regs, invoke.registers[register_cursor]));
            ++register_cursor;
        } else {
            argument_storage[argument_count++] =
                VmValue::Int(static_cast<std::int32_t>(GetFastCat1(regs, invoke.registers[register_cursor])));
            ++register_cursor;
        }
    }
    const std::span<const VmValue> arguments(argument_storage, argument_count);

    const auto callee_static = linker->Method(target_id).is_static;
    const auto callee_kind = linker->Method(target_id).kind;
    const auto callee_owner = linker->Method(target_id).owner;
    linker->EnsureClassLinked(callee_owner);
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
        PushInterpretedFrame(execution, linker->Method(target_id), arguments, instruction.width);
        goto yield;
    case MethodKind::intrinsic: {
        const auto& method = linker->Method(target_id);
        const auto rest = method.is_static ? arguments : arguments.subspan(1);
        const MethodMonitorScope monitor(*this, method, arguments);
        OGPLAY_RECORD_TRACE(DexVmTraceKind::method_enter, execution, &method, frame.pc);
        OGPLAY_SYNC_EXECUTION();
        try {
            frame.last_result = InvokeIntrinsic(linker->Method(target_id), receiver, rest);
            ticks = execution.ticks;
            OGPLAY_RECORD_TRACE(DexVmTraceKind::method_exit, execution, &linker->Method(target_id), frame.pc);
        } catch (const VmJavaThrow& thrown) {
            ticks = execution.ticks;
            if (thrown.existing.IsValid())
                SetPendingExisting(thrown.existing);
            else
                ThrowJava(thrown.descriptor, thrown.message);
            OGPLAY_RECORD_TRACE(DexVmTraceKind::method_exit, execution, nullptr, frame.pc, 0, 1);
            goto yield;
        } catch (...) {
            ticks = execution.ticks;
            OGPLAY_RECORD_TRACE(DexVmTraceKind::method_exit, execution, nullptr, frame.pc, 0, 1);
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
            throw DexVmError(DexVmErrorReason::native_bridge_unavailable,
                             "native method requires the JNI bridge: " + linker->Class(method.owner).descriptor + "." +
                                 method.name + method.descriptor);
        }
        const auto rest = method.is_static ? arguments : arguments.subspan(1);
        ++stats.native_calls;
        const MethodMonitorScope monitor(*this, method, arguments);
        const NativeFrame native_frame(*this);
        OGPLAY_RECORD_TRACE(DexVmTraceKind::native_enter, execution, &method, frame.pc);
        OGPLAY_RECORD_TRACE(DexVmTraceKind::method_enter, execution, &method, frame.pc);
        OGPLAY_SYNC_EXECUTION();
        try {
            frame.last_result = InvokeNativeWithResources(linker->Method(target_id), receiver, rest);
            ticks = execution.ticks;
            OGPLAY_RECORD_TRACE(DexVmTraceKind::native_exit, execution, &linker->Method(target_id), frame.pc);
            OGPLAY_RECORD_TRACE(DexVmTraceKind::method_exit, execution, &linker->Method(target_id), frame.pc);
        } catch (...) {
            ticks = execution.ticks;
            OGPLAY_RECORD_TRACE(DexVmTraceKind::native_exit, execution, nullptr, frame.pc, 0, 1);
            OGPLAY_RECORD_TRACE(DexVmTraceKind::method_exit, execution, nullptr, frame.pc, 0, 1);
            throw;
        }
        if (pending_exception.IsValid()) goto yield;
        continue_same_frame = true;
        break;
    }
    case MethodKind::abstract:
        RecordUnimplementedAbstract(linker->Method(target_id), receiver);
        throw JavaLinkageError(JavaLinkageKind::abstract_method, linker->Method(target_id).name);
    }
}
if (continue_same_frame) {
    OGPLAY_DISPATCH_NEXT();
}
FailCode("unreachable invoke method kind");
