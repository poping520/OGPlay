#include "boundary_symbols.h"

#include <cstdint>

namespace ogplay::runtime::detail {
namespace {

constexpr std::uint32_t kThunkStride = 4U;

}  // namespace

SupervisorCallProgress ClassifyAndroidBoundaryProgress(
    const std::string_view library, const std::string_view name,
    const std::uint32_t result) noexcept {
    // Auditable HLE progress table. Everything absent is conservatively idle.
    if (library == "libEGL.so" && name == "eglSwapBuffers" && result != 0U) {
        return SupervisorCallProgress::handled_advanced;
    }
    if (library == "libOpenSLES.so" && name == "$BufferQueue.Enqueue" &&
        result == 0U) {
        return SupervisorCallProgress::handled_advanced;
    }
    return SupervisorCallProgress::handled_idle;
}

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
