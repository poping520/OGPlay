#include "ogplay/hal/rgb_surface_storage.h"

#include <stdexcept>

namespace ogplay::hal {
bool HasRgbSurfaceStorage() noexcept { return false; }
std::unique_ptr<RgbSurfaceStorage> CreateRgbSurfaceStorage(std::uint32_t, std::uint32_t) {
    throw std::runtime_error("host RGB client-buffer storage is unavailable");
}
}  // namespace ogplay::hal
