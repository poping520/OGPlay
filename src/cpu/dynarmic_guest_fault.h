#pragma once

#include <cstdint>

namespace ogplay::cpu::detail {
// Private protocol between the checked translation extension and our callback.
// The low byte carries the faulting Thumb IT state (not its successor). Using a
// tagged exception avoids the x64-only Interpret terminal and preserves state
// without depending on a JIT backend's implementation of CPSR writes.
inline constexpr std::uint32_t kUnsupportedInstructionTag = 0x4f470000U;
inline constexpr std::uint32_t kItCpsrMask = 0x0600fc00U;

[[nodiscard]] constexpr bool IsUnsupportedInstruction(std::uint32_t value) noexcept {
    return (value & 0xffffff00U) == kUnsupportedInstructionTag;
}

[[nodiscard]] constexpr std::uint32_t RestoreItState(std::uint32_t cpsr,
                                                   std::uint32_t tagged) noexcept {
    const auto it = tagged & 0xffU;
    return (cpsr & ~kItCpsrMask) | ((it & 3U) << 25U) | ((it & 0xfcU) << 8U);
}
} // namespace ogplay::cpu::detail
