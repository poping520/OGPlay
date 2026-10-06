// Included by the checked A32 ARM64 address-space overlay. VMRS APSR directly
// following a compare can use its typed NZCV value; the FPSCR store remains.
static void ForwardAdjacentFpscrNZCV(Dynarmic::IR::Block& block) {
    using Dynarmic::IR::Opcode;
    Dynarmic::A32::IREmitter ir{block, Dynarmic::A32::LocationDescriptor{block.Location()}, {}};
    for (auto inst = block.begin(); inst != block.end(); ++inst) {
        if (inst->GetOpcode() != Opcode::A32SetFpscrNZCV) continue;
        auto get = std::next(inst);
        if (get == block.end() || get->GetOpcode() != Opcode::A32GetFpscrNZCV) continue;
        auto set = std::next(get);
        if (set == block.end() || set->GetOpcode() != Opcode::A32SetCpsrNZCVRaw) continue;
        const auto value = set->GetArg(0);
        if (value.IsImmediate() || value.GetInst() != &*get) continue;
        ir.SetInsertionPointBefore(set);
        ir.SetCpsrNZCV(Dynarmic::IR::NZCV{inst->GetArg(0)});
        set->Invalidate();
    }
}
