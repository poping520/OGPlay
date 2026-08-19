// Direct FastCode handlers for moves, constants, returns, branches,
// comparisons and arithmetic. Java-visible semantics remain shared through
// the same checked register helpers and ExecuteArithmetic implementation.

#include "interpreter_internal.h"

namespace ogplay::runtime::dexvm {

void Interpreter::Impl::StepStraight(
    InterpreterExecutionState& execution, const FastCode& code,
    const FastInstruction& instruction) {
    auto& frames = execution.frames;
    auto& pending_exception = execution.pending_exception;
    auto& exit_result = execution.exit_result;
    auto& frame = frames.back();
    const auto opcode = instruction.opcode;
    Tick(execution);
    ++stats.executed_instructions;
    if (!trace_ring.empty()) {
        RecordTrace(DexVmTraceKind::instruction, execution, frame.method,
                    instruction.dex_pc, opcode);
    }
    const auto advance = [&] { frame.pc += instruction.width; };
    const auto target_pc = [&] {
        return code.instructions[instruction.branch_target].dex_pc;
    };

    if (ExecuteArithmetic(frame, opcode, 0, &instruction)) {
        if (!pending_exception.IsValid()) advance();
        return;
    }

    switch (opcode) {
        case 0x00:
            advance();
            return;
        case 0x01:
        case 0x02:
        case 0x03:
            SetFastCat1(frame, instruction.a, GetFastCat1(frame, instruction.b));
            advance();
            return;
        case 0x04:
        case 0x05:
        case 0x06:
            SetFastWide(frame, instruction.a, GetFastWide(frame, instruction.b));
            advance();
            return;
        case 0x07:
        case 0x08:
        case 0x09:
            SetFastRef(frame, instruction.a, GetFastRef(frame, instruction.b));
            advance();
            return;
        case 0x0a:
            if (frame.last_result.kind != VmValue::Kind::cat1) {
                FailCode("move-result without cat1 result");
            }
            SetFastCat1(frame, instruction.a, frame.last_result.cat1);
            advance();
            return;
        case 0x0b:
            if (frame.last_result.kind != VmValue::Kind::wide) {
                FailCode("move-result-wide without wide result");
            }
            SetFastWide(frame, instruction.a, frame.last_result.wide);
            advance();
            return;
        case 0x0c:
            if (frame.last_result.kind != VmValue::Kind::ref) {
                FailCode("move-result-object without reference result");
            }
            SetFastRef(frame, instruction.a, frame.last_result.ref);
            advance();
            return;
        case 0x0d:
            if (!frame.caught.IsValid()) {
                FailCode("move-exception without caught exception");
            }
            SetFastRef(frame, instruction.a, frame.caught);
            frame.caught = VmObjectRef{};
            advance();
            return;
        case 0x0e:
        case 0x0f:
        case 0x10:
        case 0x11: {
            VmValue result;
            if (opcode == 0x0f) {
                result = VmValue::Int(static_cast<std::int32_t>(
                    GetFastCat1(frame, instruction.a)));
            } else if (opcode == 0x10) {
                result = VmValue::Long(static_cast<std::int64_t>(
                    GetFastWide(frame, instruction.a)));
            } else if (opcode == 0x11) {
                result = VmValue::Ref(GetFastRef(frame, instruction.a));
            }
            exit_result = result;
            RecordTrace(DexVmTraceKind::method_exit, execution, frame.method,
                        instruction.dex_pc, opcode);
            ReleaseFrameMonitor(frame);
            frames.pop_back();
            if (!frames.empty()) {
                auto& caller = frames.back();
                caller.last_result = result;
                caller.pc += caller.pending_advance;
                caller.pending_advance = 0;
            }
            return;
        }
        case 0x12:
        case 0x13:
        case 0x14:
            SetFastCat1(frame, instruction.a,
                    static_cast<std::uint32_t>(instruction.extra));
            advance();
            return;
        case 0x15:
            SetFastCat1(frame, instruction.a,
                    static_cast<std::uint32_t>(instruction.extra) << 16U);
            advance();
            return;
        case 0x16:
        case 0x17:
            SetFastWide(frame, instruction.a,
                    static_cast<std::uint64_t>(static_cast<std::int64_t>(
                        static_cast<std::int32_t>(instruction.extra))));
            advance();
            return;
        case 0x18:
            SetFastWide(frame, instruction.a, instruction.extra);
            advance();
            return;
        case 0x19:
            SetFastWide(frame, instruction.a, instruction.extra << 48U);
            advance();
            return;
        case 0x1a:
        case 0x1b: {
            const auto index = static_cast<std::uint32_t>(instruction.extra);
            PrepareSafeAllocation(
                JavaObjectModel::EstimateStringBytes(
                    linker->Image().strings[index].value.size()),
                opcode == 0x1a ? "const-string" : "const-string-jumbo");
            SetFastRef(frame, instruction.a, InternDexString(index));
            advance();
            return;
        }
        case 0x1c:
            SetFastRef(frame, instruction.a,
                   model->ClassObject(linker->ResolveTypeIndex(
                       static_cast<std::uint32_t>(instruction.extra))));
            advance();
            return;
        case 0x28:
        case 0x29:
        case 0x2a:
            frame.pc = target_pc();
            return;
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37: {
            const auto& lhs_slot = frame.regs[instruction.a];
            const auto& rhs_slot = frame.regs[instruction.b];
            std::int32_t lhs{};
            std::int32_t rhs{};
            if (lhs_slot.tag == SlotTag::ref || rhs_slot.tag == SlotTag::ref) {
                if (opcode != 0x32 && opcode != 0x33) {
                    FailCode("reference compared with ordered if");
                }
                lhs = static_cast<std::int32_t>(
                    GetFastRef(frame, instruction.a).Value());
                rhs = static_cast<std::int32_t>(
                    GetFastRef(frame, instruction.b).Value());
            } else {
                lhs = static_cast<std::int32_t>(
                    GetFastCat1(frame, instruction.a));
                rhs = static_cast<std::int32_t>(
                    GetFastCat1(frame, instruction.b));
            }
            const bool taken =
                opcode == 0x32 ? lhs == rhs
                : opcode == 0x33 ? lhs != rhs
                : opcode == 0x34 ? lhs < rhs
                : opcode == 0x35 ? lhs >= rhs
                : opcode == 0x36 ? lhs > rhs
                                 : lhs <= rhs;
            frame.pc = taken ? target_pc()
                             : instruction.dex_pc + instruction.width;
            return;
        }
        case 0x38:
        case 0x39:
        case 0x3a:
        case 0x3b:
        case 0x3c:
        case 0x3d: {
            const auto& slot = frame.regs[instruction.a];
            std::int32_t value{};
            if (slot.tag == SlotTag::ref) {
                if (opcode != 0x38 && opcode != 0x39) {
                    FailCode("reference compared with ordered ifz");
                }
                value = slot.bits == 0 ? 0 : 1;
            } else {
                value = static_cast<std::int32_t>(
                    GetFastCat1(frame, instruction.a));
            }
            const bool taken =
                opcode == 0x38 ? value == 0
                : opcode == 0x39 ? value != 0
                : opcode == 0x3a ? value < 0
                : opcode == 0x3b ? value >= 0
                : opcode == 0x3c ? value > 0
                                 : value <= 0;
            frame.pc = taken ? target_pc()
                             : instruction.dex_pc + instruction.width;
            return;
        }
        default:
            FailCode("non-straight opcode reached straight handler");
    }
}

}  // namespace ogplay::runtime::dexvm

