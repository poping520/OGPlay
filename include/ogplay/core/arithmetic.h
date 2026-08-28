#pragma once

#include <bit>
#include <cstdint>
#include <limits>
#include <optional>

namespace ogplay::core {

[[nodiscard]] constexpr std::optional<std::uint64_t> AlignUp(
    const std::uint64_t value, const std::uint64_t alignment) noexcept {
    if (!std::has_single_bit(alignment)) return std::nullopt;
    const auto mask = alignment - 1U;
    if (value > (std::numeric_limits<std::uint64_t>::max)() - mask) {
        return std::nullopt;
    }
    return (value + mask) & ~mask;
}

}  // namespace ogplay::core
