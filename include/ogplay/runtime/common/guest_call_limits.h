#pragma once
#include <cstdint>
namespace ogplay::runtime {
// Finite watchdog allowance, independent of title identity or display settings.
inline constexpr std::uint64_t kDefaultMaximumTicksPerCall = UINT64_C(10000000000);
}
