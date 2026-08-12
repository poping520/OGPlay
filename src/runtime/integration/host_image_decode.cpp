// stb_image implementation unit plus the ARGB conversion wrapper. The stb
// header is vendored (public domain) and compiled with warnings disabled,
// mirroring src/agent/stb_image_write.cpp.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include <stb_image.h>

#include "ogplay/runtime/integration/host_image_decode.h"

namespace ogplay::runtime {

std::optional<DecodedImage> DecodeImageToArgb(
    const std::span<const std::byte> bytes) {
    if (bytes.empty() || bytes.size() > INT32_MAX) return std::nullopt;
    int width{};
    int height{};
    int channels{};
    stbi_uc* rgba = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(bytes.data()),
        static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (rgba == nullptr || width <= 0 || height <= 0) {
        if (rgba != nullptr) stbi_image_free(rgba);
        return std::nullopt;
    }
    DecodedImage image;
    image.width = width;
    image.height = height;
    const auto count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    image.argb.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        const stbi_uc* pixel = rgba + index * 4;
        image.argb[index] = (static_cast<std::uint32_t>(pixel[3]) << 24U) |
                            (static_cast<std::uint32_t>(pixel[0]) << 16U) |
                            (static_cast<std::uint32_t>(pixel[1]) << 8U) |
                            static_cast<std::uint32_t>(pixel[2]);
    }
    stbi_image_free(rgba);
    return image;
}

}  // namespace ogplay::runtime
