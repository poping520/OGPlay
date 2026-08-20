    if ((opcode >= 0x2dU && opcode <= 0x31U) || opcode >= 0x7bU) {
        if (ExecuteArithmetic(frame, opcode, 0, &instruction)) {
            if (pending_exception.IsValid()) goto yield;
            OGPLAY_DISPATCH_NEXT();
        }
    }

    switch (opcode) {
        case 0x00:
            OGPLAY_DISPATCH_NEXT();
        case 0x01:
        case 0x02:
        case 0x03:
            SetFastCat1(regs, instruction.a, GetFastCat1(regs, instruction.b));
            OGPLAY_DISPATCH_NEXT();
        case 0x04:
        case 0x05:
        case 0x06:
            SetFastWide(regs, instruction.a, GetFastWide(regs, instruction.b));
            OGPLAY_DISPATCH_NEXT();
        case 0x07:
        case 0x08:
        case 0x09:
            SetFastRef(regs, instruction.a, GetFastRef(regs, instruction.b));
            OGPLAY_DISPATCH_NEXT();
        case 0x0a:
            if (frame.last_result.kind != VmValue::Kind::cat1) {
                FailCode("move-result without cat1 result");
            }
            SetFastCat1(regs, instruction.a, frame.last_result.cat1);
            OGPLAY_DISPATCH_NEXT();
        case 0x0b:
            if (frame.last_result.kind != VmValue::Kind::wide) {
                FailCode("move-result-wide without wide result");
            }
            SetFastWide(regs, instruction.a, frame.last_result.wide);
            OGPLAY_DISPATCH_NEXT();
        case 0x0c:
            if (frame.last_result.kind != VmValue::Kind::ref) {
                FailCode("move-result-object without reference result");
            }
            SetFastRef(regs, instruction.a, frame.last_result.ref);
            OGPLAY_DISPATCH_NEXT();
        case 0x0d:
            if (!frame.caught.IsValid()) {
                FailCode("move-exception without caught exception");
            }
            SetFastRef(regs, instruction.a, frame.caught);
            frame.caught = VmObjectRef{};
            OGPLAY_DISPATCH_NEXT();
        case 0x0e:
        case 0x0f:
        case 0x10:
        case 0x11: {
            VmValue result;
            if (opcode == 0x0f) {
                result = VmValue::Int(static_cast<std::int32_t>(
                    GetFastCat1(regs, instruction.a)));
            } else if (opcode == 0x10) {
                result = VmValue::Long(static_cast<std::int64_t>(
                    GetFastWide(regs, instruction.a)));
            } else if (opcode == 0x11) {
                result = VmValue::Ref(GetFastRef(regs, instruction.a));
            }
            exit_result = result;
            OGPLAY_RECORD_TRACE(DexVmTraceKind::method_exit, execution, frame.method,
                        instruction.dex_pc, opcode);
            ReleaseFrameMonitor(frame);
            frames.pop_back();
            if (!frames.empty()) {
                auto& caller = frames.back();
                caller.last_result = result;
                caller.pc += caller.pending_advance;
                caller.pending_advance = 0;
            }
            goto yield;
        }
        case 0x12:
        case 0x13:
        case 0x14:
            SetFastCat1(regs, instruction.a,
                    static_cast<std::uint32_t>(instruction.extra));
            OGPLAY_DISPATCH_NEXT();
        case 0x15:
            SetFastCat1(regs, instruction.a,
                    static_cast<std::uint32_t>(instruction.extra) << 16U);
            OGPLAY_DISPATCH_NEXT();
        case 0x16:
        case 0x17:
            SetFastWide(regs, instruction.a,
                    static_cast<std::uint64_t>(static_cast<std::int64_t>(
                        static_cast<std::int32_t>(instruction.extra))));
            OGPLAY_DISPATCH_NEXT();
        case 0x18:
            SetFastWide(regs, instruction.a, instruction.extra);
            OGPLAY_DISPATCH_NEXT();
        case 0x19:
            SetFastWide(regs, instruction.a, instruction.extra << 48U);
            OGPLAY_DISPATCH_NEXT();
        case 0x1a:
        case 0x1b: {
            const auto index = static_cast<std::uint32_t>(instruction.extra);
            PrepareSafeAllocation(
                JavaObjectModel::EstimateStringBytes(
                    linker->Image().strings[index].value.size()),
                opcode == 0x1a ? "const-string" : "const-string-jumbo");
            SetFastRef(regs, instruction.a, InternDexString(index));
            OGPLAY_DISPATCH_NEXT();
        }
        case 0x1c:
            SetFastRef(regs, instruction.a,
                   model->ClassObject(linker->ResolveTypeIndex(
                       static_cast<std::uint32_t>(instruction.extra))));
            OGPLAY_DISPATCH_NEXT();
        case 0x28:
        case 0x29:
        case 0x2a:
            OGPLAY_DISPATCH_TO(instruction.branch_target);
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37: {
            const auto& lhs_slot = regs[instruction.a];
            const auto& rhs_slot = regs[instruction.b];
            std::int32_t lhs{};
            std::int32_t rhs{};
            if (lhs_slot.tag == SlotTag::ref || rhs_slot.tag == SlotTag::ref) {
                if (opcode != 0x32 && opcode != 0x33) {
                    FailCode("reference compared with ordered if");
                }
                lhs = static_cast<std::int32_t>(
                    GetFastRef(regs, instruction.a).Value());
                rhs = static_cast<std::int32_t>(
                    GetFastRef(regs, instruction.b).Value());
            } else {
                lhs = static_cast<std::int32_t>(
                    GetFastCat1(regs, instruction.a));
                rhs = static_cast<std::int32_t>(
                    GetFastCat1(regs, instruction.b));
            }
            const bool taken =
                opcode == 0x32 ? lhs == rhs
                : opcode == 0x33 ? lhs != rhs
                : opcode == 0x34 ? lhs < rhs
                : opcode == 0x35 ? lhs >= rhs
                : opcode == 0x36 ? lhs > rhs
                                 : lhs <= rhs;
            OGPLAY_DISPATCH_TO(taken ? instruction.branch_target : ip + 1U);
        }
        case 0x38:
        case 0x39:
        case 0x3a:
        case 0x3b:
        case 0x3c:
        case 0x3d: {
            const auto& slot = regs[instruction.a];
            std::int32_t value{};
            if (slot.tag == SlotTag::ref) {
                if (opcode != 0x38 && opcode != 0x39) {
                    FailCode("reference compared with ordered ifz");
                }
                value = slot.bits == 0 ? 0 : 1;
            } else {
                value = static_cast<std::int32_t>(
                    GetFastCat1(regs, instruction.a));
            }
            const bool taken =
                opcode == 0x38 ? value == 0
                : opcode == 0x39 ? value != 0
                : opcode == 0x3a ? value < 0
                : opcode == 0x3b ? value >= 0
                : opcode == 0x3c ? value > 0
                                 : value <= 0;
            OGPLAY_DISPATCH_TO(taken ? instruction.branch_target : ip + 1U);
        }
        default:
            FailCode("non-straight opcode reached straight handler");
    }
