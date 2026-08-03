#include "ogplay/cpu/interpreter.h"

#include <bit>
#include <cstdint>
#include <optional>

namespace ogplay::cpu {
namespace {

inline constexpr std::uint32_t kNegativeFlag = 1U << 31U;
inline constexpr std::uint32_t kZeroFlag = 1U << 30U;
inline constexpr std::uint32_t kCarryFlag = 1U << 29U;
inline constexpr std::uint32_t kOverflowFlag = 1U << 28U;
inline constexpr std::uint32_t kStatusFlags = kNegativeFlag | kZeroFlag |
                                              kCarryFlag | kOverflowFlag;

struct ArithmeticResult final {
    std::uint32_t value{};
    bool carry{};
    bool overflow{};
};

[[nodiscard]] bool Flag(const A32State& state, const std::uint32_t flag) {
    return (state.Cpsr() & flag) != 0;
}

[[nodiscard]] bool ConditionPassed(const A32State& state, const std::uint8_t condition) {
    const bool negative = Flag(state, kNegativeFlag);
    const bool zero = Flag(state, kZeroFlag);
    const bool carry = Flag(state, kCarryFlag);
    const bool overflow = Flag(state, kOverflowFlag);
    switch (condition) {
    case 0x0: return zero;
    case 0x1: return !zero;
    case 0x2: return carry;
    case 0x3: return !carry;
    case 0x4: return negative;
    case 0x5: return !negative;
    case 0x6: return overflow;
    case 0x7: return !overflow;
    case 0x8: return carry && !zero;
    case 0x9: return !carry || zero;
    case 0xa: return negative == overflow;
    case 0xb: return negative != overflow;
    case 0xc: return !zero && negative == overflow;
    case 0xd: return zero || negative != overflow;
    case 0xe: return true;
    default: return false;
    }
}

[[nodiscard]] ArithmeticResult AddWithCarry(const std::uint32_t left,
                                             const std::uint32_t right,
                                             const bool carry_in) {
    const auto wide = static_cast<std::uint64_t>(left) + right +
                      static_cast<std::uint64_t>(carry_in);
    const auto value = static_cast<std::uint32_t>(wide);
    const bool overflow = ((~(left ^ right) & (left ^ value)) & kNegativeFlag) != 0;
    return {value, (wide >> 32U) != 0, overflow};
}

void SetArithmeticFlags(A32State& state, const ArithmeticResult result) {
    auto cpsr = state.Cpsr() & ~kStatusFlags;
    if ((result.value & kNegativeFlag) != 0) cpsr |= kNegativeFlag;
    if (result.value == 0) cpsr |= kZeroFlag;
    if (result.carry) cpsr |= kCarryFlag;
    if (result.overflow) cpsr |= kOverflowFlag;
    state.SetCpsr(cpsr);
}

void SetMoveFlags(A32State& state, const std::uint32_t value,
                  const std::optional<bool> carry) {
    auto cpsr = state.Cpsr() & ~(kNegativeFlag | kZeroFlag);
    if ((value & kNegativeFlag) != 0) cpsr |= kNegativeFlag;
    if (value == 0) cpsr |= kZeroFlag;
    if (carry.has_value()) {
        cpsr &= ~kCarryFlag;
        if (*carry) cpsr |= kCarryFlag;
    }
    state.SetCpsr(cpsr);
}

[[nodiscard]] std::int32_t SignExtend(const std::uint32_t value,
                                      const unsigned bits) {
    const auto sign = UINT32_C(1) << (bits - 1U);
    const auto mask = (UINT32_C(1) << bits) - 1U;
    const auto narrowed = value & mask;
    return static_cast<std::int32_t>((narrowed ^ sign) - sign);
}

[[nodiscard]] std::uint32_t AddSigned(const std::uint32_t base,
                                      const std::int64_t displacement) {
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(base) + displacement);
}

[[nodiscard]] std::uint32_t ReadOperandRegister(const A32State& state,
                                                const std::uint8_t index,
                                                const std::uint32_t pc,
                                                const std::uint32_t pc_bias) {
    if (index == static_cast<std::uint8_t>(CoreRegister::pc)) return pc + pc_bias;
    return state.Register(static_cast<CoreRegister>(index));
}

[[nodiscard]] RunResult Stop(const RunStopReason reason, const memory::GuestAddress pc,
                             const std::uint32_t instruction,
                             const std::uint32_t immediate = 0) {
    return {1, reason, pc, instruction, immediate, std::nullopt};
}

[[nodiscard]] RunResult Undefined(const memory::GuestAddress pc,
                                  const std::uint32_t instruction) {
    return Stop(RunStopReason::undefined_instruction, pc, instruction);
}

}  // namespace

InterpreterCpu::InterpreterCpu(memory::MemoryBus& memory_bus) noexcept
    : memory_bus_(memory_bus) {}

RunResult InterpreterCpu::Run(const std::uint64_t tick_budget) {
    std::uint64_t consumed = 0;
    while (consumed < tick_budget) {
        const auto pc_value = state_.Register(CoreRegister::pc);
        const memory::GuestAddress pc{pc_value};
        if (halt_requested_.exchange(false)) {
            return {consumed, RunStopReason::halt_requested, pc};
        }

        std::uint32_t instruction = 0;
        try {
            std::optional<RunResult> stop;
            if (state_.State() == ExecutionState::a32) {
                if ((pc_value & 3U) != 0) return Undefined(pc, 0);
                instruction = memory_bus_.Fetch32(pc, state_.ThreadId());
                stop = ExecuteA32(pc, instruction);
            } else {
                if ((pc_value & 1U) != 0) return Undefined(pc, 0);
                const auto first = memory_bus_.Fetch16(pc, state_.ThreadId());
                instruction = first;
                if ((first & 0xf800U) >= 0xe800U) {
                    const auto second = memory_bus_.Fetch16(
                        memory::GuestAddress{pc_value + 2U}, state_.ThreadId());
                    instruction |= static_cast<std::uint32_t>(second) << 16U;
                    stop = Undefined(pc, instruction);
                } else {
                    stop = ExecuteThumb(pc, first);
                }
            }
            if (stop.has_value()) {
                stop->ticks_consumed += consumed;
                return *stop;
            }
            ++consumed;
        } catch (const memory::MemoryFault& fault) {
            return {consumed,
                    RunStopReason::memory_fault,
                    pc,
                    instruction,
                    0,
                    CpuFault{fault.Address(), fault.Access(), fault.Reason(),
                             fault.ThreadId()}};
        }
    }
    return {consumed, RunStopReason::budget_exhausted,
            memory::GuestAddress{state_.Register(CoreRegister::pc)}};
}

A32State InterpreterCpu::GetState() const { return state_; }
void InterpreterCpu::SetState(const A32State& state) { state_ = state; }
void InterpreterCpu::RequestHalt() noexcept { halt_requested_.store(true); }

std::optional<RunResult> InterpreterCpu::ExecuteA32(
    const memory::GuestAddress pc, const std::uint32_t instruction) {
    const auto condition = static_cast<std::uint8_t>(instruction >> 28U);
    if (condition == 0xfU) return Undefined(pc, instruction);
    const auto next = pc.Value() + 4U;
    if (!ConditionPassed(state_, condition)) {
        state_.SetRegister(CoreRegister::pc, next);
        return std::nullopt;
    }

    if ((instruction & 0x0f000000U) == 0x0f000000U) {
        state_.SetRegister(CoreRegister::pc, next);
        return Stop(RunStopReason::supervisor_call, pc, instruction,
                    instruction & 0x00ffffffU);
    }
    if ((instruction & 0x0ffffff0U) == 0x012fff10U) {
        const auto index = static_cast<std::uint8_t>(instruction & 0xfU);
        const auto target = ReadOperandRegister(state_, index, pc.Value(), 8);
        const auto target_state = (target & 1U) == 0 ? ExecutionState::a32
                                                     : ExecutionState::thumb;
        state_.SetState(target_state);
        state_.SetRegister(CoreRegister::pc,
                           target_state == ExecutionState::a32 ? target & ~3U
                                                               : target & ~1U);
        return std::nullopt;
    }
    if ((instruction & 0x0e000000U) == 0x0a000000U) {
        const auto displacement = static_cast<std::int64_t>(
            SignExtend(instruction & 0x00ffffffU, 24)) * 4;
        if ((instruction & (1U << 24U)) != 0) {
            state_.SetRegister(CoreRegister::lr, next);
        }
        state_.SetRegister(CoreRegister::pc,
                           AddSigned(pc.Value() + 8U, displacement));
        return std::nullopt;
    }
    if ((instruction & 0x0c000000U) != 0) return Undefined(pc, instruction);

    const bool immediate = (instruction & (1U << 25U)) != 0;
    std::uint32_t operand2 = 0;
    std::optional<bool> shifter_carry;
    if (immediate) {
        const auto rotate = static_cast<unsigned>((instruction >> 8U) & 0xfU) * 2U;
        operand2 = std::rotr(instruction & 0xffU, static_cast<int>(rotate));
        if (rotate != 0) shifter_carry = (operand2 & kNegativeFlag) != 0;
    } else {
        if ((instruction & 0x00000ff0U) != 0) return Undefined(pc, instruction);
        operand2 = ReadOperandRegister(
            state_, static_cast<std::uint8_t>(instruction & 0xfU), pc.Value(), 8);
    }

    const auto opcode = static_cast<std::uint8_t>((instruction >> 21U) & 0xfU);
    const bool set_flags = (instruction & (1U << 20U)) != 0;
    const auto rn = static_cast<std::uint8_t>((instruction >> 16U) & 0xfU);
    const auto rd = static_cast<std::uint8_t>((instruction >> 12U) & 0xfU);
    const auto left = ReadOperandRegister(state_, rn, pc.Value(), 8);
    std::uint32_t result = 0;
    bool write_result = true;
    switch (opcode) {
    case 0x2: {
        const auto arithmetic = AddWithCarry(left, ~operand2, true);
        result = arithmetic.value;
        if (set_flags) SetArithmeticFlags(state_, arithmetic);
        break;
    }
    case 0x4: {
        const auto arithmetic = AddWithCarry(left, operand2, false);
        result = arithmetic.value;
        if (set_flags) SetArithmeticFlags(state_, arithmetic);
        break;
    }
    case 0xa: {
        if (!set_flags) return Undefined(pc, instruction);
        SetArithmeticFlags(state_, AddWithCarry(left, ~operand2, true));
        write_result = false;
        break;
    }
    case 0xd:
        result = operand2;
        if (set_flags) SetMoveFlags(state_, result, shifter_carry);
        break;
    default: return Undefined(pc, instruction);
    }

    if (write_result) {
        if (rd == static_cast<std::uint8_t>(CoreRegister::pc)) {
            state_.SetRegister(CoreRegister::pc, result & ~3U);
            return std::nullopt;
        }
        state_.SetRegister(static_cast<CoreRegister>(rd), result);
    }
    state_.SetRegister(CoreRegister::pc, next);
    return std::nullopt;
}

std::optional<RunResult> InterpreterCpu::ExecuteThumb(
    const memory::GuestAddress pc, const std::uint16_t instruction) {
    const auto next = pc.Value() + 2U;
    if ((instruction & 0xff00U) == 0xdf00U) {
        state_.SetRegister(CoreRegister::pc, next);
        return Stop(RunStopReason::supervisor_call, pc, instruction,
                    instruction & 0xffU);
    }
    if ((instruction & 0xff00U) == 0xbe00U) {
        state_.SetRegister(CoreRegister::pc, next);
        return Stop(RunStopReason::breakpoint, pc, instruction, instruction & 0xffU);
    }
    if (instruction == 0xbf00U) {
        state_.SetRegister(CoreRegister::pc, next);
        return std::nullopt;
    }
    if ((instruction & 0xff87U) == 0x4700U) {
        const auto index = static_cast<std::uint8_t>((instruction >> 3U) & 0xfU);
        const auto target = ReadOperandRegister(state_, index, pc.Value(), 4);
        const auto target_state = (target & 1U) == 0 ? ExecutionState::a32
                                                     : ExecutionState::thumb;
        state_.SetState(target_state);
        state_.SetRegister(CoreRegister::pc,
                           target_state == ExecutionState::a32 ? target & ~3U
                                                               : target & ~1U);
        return std::nullopt;
    }

    const auto operation = instruction & 0xf800U;
    const auto rd = static_cast<std::uint8_t>((instruction >> 8U) & 0x7U);
    const auto immediate = static_cast<std::uint32_t>(instruction & 0xffU);
    if (operation == 0x2000U) {
        state_.SetRegister(static_cast<CoreRegister>(rd), immediate);
        SetMoveFlags(state_, immediate, std::nullopt);
    } else if (operation == 0x2800U) {
        const auto left = state_.Register(static_cast<CoreRegister>(rd));
        SetArithmeticFlags(state_, AddWithCarry(left, ~immediate, true));
    } else if (operation == 0x3000U || operation == 0x3800U) {
        const auto left = state_.Register(static_cast<CoreRegister>(rd));
        const auto result = operation == 0x3000U
                                ? AddWithCarry(left, immediate, false)
                                : AddWithCarry(left, ~immediate, true);
        state_.SetRegister(static_cast<CoreRegister>(rd), result.value);
        SetArithmeticFlags(state_, result);
    } else if ((instruction & 0xf000U) == 0xd000U) {
        const auto condition = static_cast<std::uint8_t>((instruction >> 8U) & 0xfU);
        if (condition >= 0xeU) return Undefined(pc, instruction);
        if (ConditionPassed(state_, condition)) {
            const auto displacement = static_cast<std::int64_t>(
                SignExtend(instruction & 0xffU, 8)) * 2;
            state_.SetRegister(CoreRegister::pc,
                               AddSigned(pc.Value() + 4U, displacement));
            return std::nullopt;
        }
    } else if ((instruction & 0xf800U) == 0xe000U) {
        const auto displacement = static_cast<std::int64_t>(
            SignExtend(instruction & 0x7ffU, 11)) * 2;
        state_.SetRegister(CoreRegister::pc,
                           AddSigned(pc.Value() + 4U, displacement));
        return std::nullopt;
    } else {
        return Undefined(pc, instruction);
    }
    state_.SetRegister(CoreRegister::pc, next);
    return std::nullopt;
}

}  // namespace ogplay::cpu
