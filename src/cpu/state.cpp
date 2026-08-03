#include "ogplay/cpu/cpu.h"

#include <stdexcept>

namespace ogplay::cpu {
namespace {

inline constexpr std::uint32_t kUserMode = 0x10;
inline constexpr std::uint32_t kThumbBit = 1U << 5U;

[[nodiscard]] std::size_t RegisterIndex(const CoreRegister index) {
    const auto value = static_cast<std::size_t>(index);
    if (value >= 16) throw std::out_of_range("invalid A32 core register");
    return value;
}

}  // namespace

A32State::A32State() : cpsr_(kUserMode) {}

std::uint32_t A32State::Register(const CoreRegister index) const {
    return core_[RegisterIndex(index)];
}

void A32State::SetRegister(const CoreRegister index, const std::uint32_t value) {
    core_[RegisterIndex(index)] = value;
}

const std::array<std::uint32_t, 16>& A32State::CoreRegisters() const noexcept {
    return core_;
}

const std::array<std::uint32_t, 64>& A32State::ExtendedRegisters() const noexcept {
    return extended_;
}

void A32State::SetExtendedRegister(const std::uint8_t index,
                                   const std::uint32_t value) {
    if (index >= extended_.size()) {
        throw std::out_of_range("invalid A32 extended register");
    }
    extended_[index] = value;
}

std::uint32_t A32State::Cpsr() const noexcept { return cpsr_; }
void A32State::SetCpsr(const std::uint32_t value) noexcept { cpsr_ = value; }
std::uint32_t A32State::Fpscr() const noexcept { return fpscr_; }
void A32State::SetFpscr(const std::uint32_t value) noexcept { fpscr_ = value; }
std::uint32_t A32State::Fpexc() const noexcept { return fpexc_; }
void A32State::SetFpexc(const std::uint32_t value) noexcept { fpexc_ = value; }
std::uint64_t A32State::ThreadId() const noexcept { return thread_id_; }
void A32State::SetThreadId(const std::uint64_t value) noexcept { thread_id_ = value; }

ExecutionState A32State::State() const noexcept {
    return (cpsr_ & kThumbBit) == 0 ? ExecutionState::a32 : ExecutionState::thumb;
}

void A32State::SetState(const ExecutionState state) noexcept {
    if (state == ExecutionState::thumb) {
        cpsr_ |= kThumbBit;
    } else {
        cpsr_ &= ~kThumbBit;
    }
}

}  // namespace ogplay::cpu
