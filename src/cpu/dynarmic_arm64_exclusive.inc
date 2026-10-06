// Included in the checked ARM64 memory-emitter overlay, inside its anonymous
// namespace. Only scalar, aligned single-page accesses borrow published backing.
// PrepareForCall releases IR caller registers; X0-X8 are scratch until the result
// is defined. The fallback uses the original monitor/callback trampoline.
template<size_t bitsize>
void InlineExclusiveReadMemory(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.PrepareForCall({}, args[1]);
    code.MOV(W1, W1); // Canonical A32 address, including its zero upper half.
    const bool ordered = IsOrdered(args[2].GetImmediateAccType());
    oaknut::Label fallback, end;
    auto fallback_ptr = std::shared_ptr<oaknut::Label>(&fallback, [](auto*) {});

    if constexpr (bitsize > 8) {
        code.TST(X1, bitsize / 8 - 1);
        code.B(NE, fallback);
    }
    const auto [base, offset] = InlinePageTableEmitVAddrLookup<bitsize>(code, ctx, X1, fallback_ptr, true);
    code.ADD(X0, base, offset);
    code.MOV(Xscratch2, ctx.conf.exclusive_lock_pointer);
    Dynarmic::EmitSpinLockLock(code, Xscratch2);
    EmitMemoryLdr<bitsize>(code, 0, X0, XZR, false);
    code.MOV(Xscratch0, ctx.conf.exclusive_address_pointer);
    code.STR(X1, Xscratch0);
    code.MOV(Xscratch0, ctx.conf.exclusive_value_pointer);
    if constexpr (bitsize == 64) code.STR(X0, Xscratch0);
    else code.STR(W0, Xscratch0);
    Dynarmic::EmitSpinLockUnlock(code, Xscratch2);
    code.B(end);

    code.l(fallback);
    EmitRelocation(code, ctx, ExclusiveReadMemoryLinkTarget(bitsize));
    code.l(end);
    code.MOV(Wscratch0, 1);
    code.STRB(Wscratch0, Xstate, ctx.conf.state_exclusive_state_offset);
    if (ordered) code.DMB(oaknut::BarrierOp::ISH);
    ctx.reg_alloc.DefineAsRegister(inst, X0);
}

template<size_t bitsize>
void InlineExclusiveWriteMemory(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.PrepareForCall({}, args[1], args[2]);
    code.MOV(W1, W1);
    const bool ordered = IsOrdered(args[3].GetImmediateAccType());
    oaknut::Label fallback, failed, end, unlock_failed, peers, next_peer, cas_retry, cas_mismatch;
    auto fallback_ptr = std::shared_ptr<oaknut::Label>(&fallback, [](auto*) {});
    if (ordered) code.DMB(oaknut::BarrierOp::ISH);
    code.LDRB(Wscratch0, Xstate, ctx.conf.state_exclusive_state_offset);
    code.CBZ(Wscratch0, failed);
    code.STRB(WZR, Xstate, ctx.conf.state_exclusive_state_offset);

    if constexpr (bitsize > 8) {
        code.TST(X1, bitsize / 8 - 1);
        code.B(NE, fallback);
    }
    const auto [base, offset] = InlinePageTableEmitVAddrLookup<bitsize>(code, ctx, X1, fallback_ptr);
    code.ADD(X0, base, offset);
    code.MOV(Xscratch2, ctx.conf.exclusive_lock_pointer);
    Dynarmic::EmitSpinLockLock(code, Xscratch2);
    code.MOV(X3, ctx.conf.exclusive_address_pointer);
    code.LDR(X4, X3);
    code.CMP(X4, X1);
    code.B(NE, unlock_failed);

    // A monotonic published high-water bound includes every processor that can
    // have a reservation. Retired slots remain in the scan; IDs may be reused.
    code.MOV(X3, ctx.conf.exclusive_addresses_pointer);
    if (ctx.conf.exclusive_processor_bound_pointer != 0) {
        code.MOV(X4, ctx.conf.exclusive_processor_bound_pointer);
        code.LDAR(X4, X4);
    } else {
        code.MOV(X4, ctx.conf.exclusive_processor_count);
    }
    code.ADD(X4, X3, X4, LSL, 3);
    code.MOV(X6, 0xDEAD'DEAD'DEAD'DEADull);
    code.l(peers);
    code.LDR(X5, X3);
    code.CMP(X5, X1);
    code.B(NE, next_peer);
    code.STR(X6, X3);
    code.l(next_peer);
    code.ADD(X3, X3, 8);
    code.CMP(X3, X4);
    code.B(NE, peers);

    code.MOV(X3, ctx.conf.exclusive_value_pointer);
    if constexpr (bitsize == 64) code.LDR(X4, X3);
    else if constexpr (bitsize == 32) code.LDR(W4, X3);
    else if constexpr (bitsize == 16) code.LDRH(W4, X3);
    else code.LDRB(W4, X3);
    code.l(cas_retry);
    if constexpr (bitsize == 64) code.LDAXR(X5, X0);
    else if constexpr (bitsize == 32) code.LDAXR(W5, X0);
    else if constexpr (bitsize == 16) code.LDAXRH(W5, X0);
    else code.LDAXRB(W5, X0);
    if constexpr (bitsize == 64) code.CMP(X5, X4);
    else code.CMP(W5, W4);
    code.B(NE, cas_mismatch);
    if constexpr (bitsize == 64) code.STLXR(W3, X2, X0);
    else if constexpr (bitsize == 32) code.STLXR(W3, W2, X0);
    else if constexpr (bitsize == 16) code.STLXRH(W3, W2, X0);
    else code.STLXRB(W3, W2, X0);
    code.CBNZ(W3, cas_retry);
    Dynarmic::EmitSpinLockUnlock(code, Xscratch2);
    code.MOV(W0, 0);
    code.B(end);

    code.l(cas_mismatch);
    code.CLREX();
    code.l(unlock_failed);
    Dynarmic::EmitSpinLockUnlock(code, Xscratch2);
    code.l(failed);
    code.MOV(W0, 1);
    code.B(end);
    code.l(fallback);
    EmitRelocation(code, ctx, ExclusiveWriteMemoryLinkTarget(bitsize));
    code.l(end);
    if (ordered) code.DMB(oaknut::BarrierOp::ISH);
    ctx.reg_alloc.DefineAsRegister(inst, X0);
}
