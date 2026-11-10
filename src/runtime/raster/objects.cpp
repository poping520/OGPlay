#include "objects.h"

#include <atomic>
#include <limits>
#include <new>

namespace ogplay::runtime::raster {

void Camera::GetMatrix(SkMatrix& matrix) const {
    if (projection.fLocation == SkV3{0, 0, -576}) {
        view.getMatrix(&matrix);
        return;
    }
    SkPatch3D patch;
    patch.transform(transforms.back());
    projection.patchToMatrix(patch, &matrix);
}

std::uint32_t AllocateToken() {
    static std::atomic<std::uint64_t> next{1};
    const auto bits = next.fetch_add(1, std::memory_order_relaxed);
    if (bits > std::numeric_limits<std::uint32_t>::max()) throw std::bad_alloc{};
    return static_cast<std::uint32_t>(bits);
}

std::shared_ptr<GraphicsObjects> NewGraphicsObjects() {
    return std::make_shared<GraphicsObjects>();
}

}  // namespace ogplay::runtime::raster
