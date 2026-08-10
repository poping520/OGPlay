#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ogplay::gles {

[[nodiscard]] std::vector<std::byte> DecodeEtc1Rgba8(
    std::uint32_t width,
    std::uint32_t height,
    std::span<const std::byte> compressed);

}  // namespace ogplay::gles
