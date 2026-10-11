// stb_image implementation unit plus the ARGB conversion wrapper. The stb
// header is vendored (public domain) and compiled with warnings disabled,
// mirroring src/agent/stb_image_write.cpp.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include <stb_image.h>

#include "ogplay/runtime/integration/host_image_decode.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ogplay::runtime {

bool ImageDimensionsSupported(std::int32_t width, std::int32_t height) noexcept {
    return width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) <= kMaximumImagePixels;
}

std::optional<ImageInfo> ProbeImage(std::span<const std::byte> bytes) {
    if (bytes.empty() || bytes.size() > kMaximumImageEncodedBytes) return std::nullopt;
    int width{}, height{}, channels{};
    if (!stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
            static_cast<int>(bytes.size()), &width, &height, &channels)) return std::nullopt;
    const char* mime = nullptr;
    const auto byte = [&](std::size_t n) { return std::to_integer<unsigned>(bytes[n]); };
    if (bytes.size() >= 8 && byte(0) == 137 && byte(1) == 'P' && byte(2) == 'N' && byte(3) == 'G') mime = "image/png";
    else if (bytes.size() >= 2 && byte(0) == 255 && byte(1) == 216) mime = "image/jpeg";
    else if (bytes.size() >= 3 && byte(0) == 'G' && byte(1) == 'I' && byte(2) == 'F') mime = "image/gif";
    else if (bytes.size() >= 2 && byte(0) == 'B' && byte(1) == 'M') mime = "image/bmp";
    return ImageInfo{width, height, mime};
}

DecodedImage ResizeArgbImage(DecodedImage source, std::int32_t width, std::int32_t height) {
    if (!ImageDimensionsSupported(width, height)) throw std::length_error("scaled image exceeds decode budget");
    if (source.width == width && source.height == height) return source;
    DecodedImage result{width, height, std::vector<std::uint32_t>(static_cast<std::size_t>(width) * height)};
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto sx = std::clamp((x + 0.5) * source.width / width - 0.5, 0.0, static_cast<double>(source.width - 1));
        const auto sy = std::clamp((y + 0.5) * source.height / height - 0.5, 0.0, static_cast<double>(source.height - 1));
        const int x0 = static_cast<int>(sx), y0 = static_cast<int>(sy);
        const int x1 = std::min(x0 + 1, source.width - 1), y1 = std::min(y0 + 1, source.height - 1);
        const double tx = sx - x0, ty = sy - y0;
        double a{}, r{}, g{}, b{};
        const auto accumulate = [&](int px, int py, double weight) {
            const auto c = source.argb[static_cast<std::size_t>(py) * source.width + px];
            const auto alpha = static_cast<double>(c >> 24);
            a += alpha * weight; r += ((c >> 16) & 255) * alpha * weight;
            g += ((c >> 8) & 255) * alpha * weight; b += (c & 255) * alpha * weight;
        };
        accumulate(x0, y0, (1-tx)*(1-ty)); accumulate(x1, y0, tx*(1-ty));
        accumulate(x0, y1, (1-tx)*ty); accumulate(x1, y1, tx*ty);
        const auto channel = [](double v) { return static_cast<std::uint32_t>(std::clamp(std::lround(v), 0L, 255L)); };
        result.argb[static_cast<std::size_t>(y) * width + x] = (channel(a) << 24) |
            (channel(a > 0 ? r/a : 0) << 16) | (channel(a > 0 ? g/a : 0) << 8) | channel(a > 0 ? b/a : 0);
    }
    return result;
}

std::optional<DecodedImage> DecodeImageToArgb(
    const std::span<const std::byte> bytes) {
    const auto info = ProbeImage(bytes);
    if (!info || !ImageDimensionsSupported(info->width, info->height)) return std::nullopt;
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
