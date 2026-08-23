#include "boundary_symbols.h"

#include <cstdint>

namespace ogplay::runtime::detail {
namespace {

constexpr std::uint32_t kThunkStride = 4U;

}  // namespace

const HleThunkDescriptor* DecodeAndroidBoundaryThunk(
    const std::uint64_t pc,
    const std::span<const HleThunkDescriptor> descriptors) noexcept {
    const auto code = pc & ~UINT64_C(1);
    if (code < kBionicHleThunkBegin || code >= kBionicHleThunkEnd) {
        return nullptr;
    }
    const auto offset = code - kBionicHleThunkBegin;
    if (offset % kThunkStride != 0U) return nullptr;
    const auto index = offset / kThunkStride;
    if (index >= descriptors.size()) return nullptr;
    return &descriptors[static_cast<std::size_t>(index)];
}

}  // namespace ogplay::runtime::detail
