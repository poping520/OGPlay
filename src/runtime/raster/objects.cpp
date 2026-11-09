#include "objects.h"

#include <atomic>
#include <limits>
#include <new>

namespace ogplay::runtime::raster {

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
