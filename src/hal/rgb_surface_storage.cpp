#include "ogplay/hal/rgb_surface_storage.h"

#include <stdexcept>

namespace ogplay::hal {
bool HasRgbSurfaceStorage() noexcept { return false; }
bool HasPackedRgbSurfaceStorage() noexcept { return false; }
std::unique_ptr<RgbSurfaceStorage> CreatePackedRgbSurfaceStorage(void*, std::uint32_t, std::uint32_t) {
    throw std::runtime_error("host packed RGB storage is unavailable");
}
std::unique_ptr<RgbSurfaceStorage> CreatePackedDepthStencilStorage(void*, std::uint32_t, std::uint32_t) {
    throw std::runtime_error("host packed depth/stencil storage is unavailable");
}
std::unique_ptr<RgbSurfaceStorage> CreateRgbSurfaceStorage(std::uint32_t, std::uint32_t) {
    throw std::runtime_error("host RGB client-buffer storage is unavailable");
}
}  // namespace ogplay::hal
