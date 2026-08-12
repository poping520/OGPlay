#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ogplay::runtime {

// Host-side compressed image decode (PNG/JPEG/...) used by the
// BitmapFactory intrinsic. Pixels come back row-major as packed ARGB8888,
// matching what Bitmap.getPixels hands to interpreted code.
struct DecodedImage final {
    std::int32_t width{};
    std::int32_t height{};
    std::vector<std::uint32_t> argb;
};

// Returns nullopt when the payload is not a decodable image; the caller
// maps that to the documented BitmapFactory null result.
[[nodiscard]] std::optional<DecodedImage> DecodeImageToArgb(
    std::span<const std::byte> bytes);

}  // namespace ogplay::runtime
