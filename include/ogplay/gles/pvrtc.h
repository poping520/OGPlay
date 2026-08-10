#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ogplay::gles {

[[nodiscard]] std::vector<std::byte> DecodePvrtc1Rgba8(
    std::uint32_t width, std::uint32_t height, std::uint8_t bits_per_pixel,
    bool opaque, std::span<const std::byte> compressed);

}  // namespace ogplay::gles
