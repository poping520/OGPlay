#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ogplay::gles {
inline constexpr std::uint32_t kAtcRgb = 0x8c92U;
inline constexpr std::uint32_t kAtcRgbaExplicit = 0x8c93U;
inline constexpr std::uint32_t kAtcRgbaInterpolated = 0x87eeU;
[[nodiscard]] constexpr bool IsAtcFormat(const std::uint32_t format) noexcept {
    return format == kAtcRgb || format == kAtcRgbaExplicit ||
           format == kAtcRgbaInterpolated;
}
// Compressed block sizes are independent of guest pixel-store alignment.
[[nodiscard]] std::size_t AtcImageBytes(std::uint32_t width, std::uint32_t height,
                                       std::uint32_t format);
[[nodiscard]] std::vector<std::byte> DecodeAtcRgba8(
    std::uint32_t width, std::uint32_t height, std::uint32_t format,
    std::span<const std::byte> compressed);
}  // namespace ogplay::gles
