#pragma once

#include <cstdint>
#include <memory>

namespace ogplay::hal {

// Host-only color/depth storage for ANGLE opaque client-buffer APIs.
class RgbSurfaceStorage {
public:
    virtual ~RgbSurfaceStorage() = default;
    [[nodiscard]] virtual void* NativeBuffer() const noexcept = 0;
};

[[nodiscard]] bool HasRgbSurfaceStorage() noexcept;
[[nodiscard]] std::unique_ptr<RgbSurfaceStorage> CreateRgbSurfaceStorage(
    std::uint32_t width, std::uint32_t height);
// Native Metal texture storage; never accepts a guest device pointer.
[[nodiscard]] bool HasPackedRgbSurfaceStorage() noexcept;
[[nodiscard]] std::unique_ptr<RgbSurfaceStorage> CreatePackedRgbSurfaceStorage(
    void* native_device, std::uint32_t width, std::uint32_t height);

[[nodiscard]] std::unique_ptr<RgbSurfaceStorage> CreatePackedDepthStencilStorage(
    void* native_device, std::uint32_t width, std::uint32_t height);

}  // namespace ogplay::hal
