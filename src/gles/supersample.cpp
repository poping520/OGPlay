#include "ogplay/gles/supersample.h"

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ogplay::gles {
namespace {

constexpr std::uint32_t kMaximumSupersampleFactor = 4;
constexpr std::size_t kRgbaChannels = 4;

std::size_t RgbaByteCount(const std::uint32_t width,
                          const std::uint32_t height) {
    const auto pixels = static_cast<std::uint64_t>(width) * height;
    if (pixels > std::numeric_limits<std::size_t>::max() / kRgbaChannels) {
        throw std::overflow_error("supersample RGBA8 byte count overflows");
    }
    return static_cast<std::size_t>(pixels * kRgbaChannels);
}

}  // namespace

SupersampleLayout MakeSupersampleLayout(
    const std::uint32_t logical_width, const std::uint32_t logical_height,
    const std::uint32_t factor) {
    if (logical_width == 0 || logical_height == 0) {
        throw std::invalid_argument("supersample logical dimensions must be non-zero");
    }
    if (factor == 0 || factor > kMaximumSupersampleFactor) {
        throw std::invalid_argument("supersample factor must be in 1..4");
    }
    if (logical_width > std::numeric_limits<std::uint32_t>::max() / factor ||
        logical_height > std::numeric_limits<std::uint32_t>::max() / factor) {
        throw std::overflow_error("supersample render dimensions overflow");
    }
    return {logical_width, logical_height,
            logical_width * factor, logical_height * factor, factor};
}

std::vector<std::uint8_t> ResolveSupersampledRgba8(
    const std::span<const std::uint8_t> pixels,
    const SupersampleLayout& layout) {
    if (MakeSupersampleLayout(layout.logical_width, layout.logical_height,
                              layout.factor) != layout) {
        throw std::invalid_argument("supersample layout is inconsistent");
    }
    if (pixels.size() != RgbaByteCount(layout.render_width, layout.render_height)) {
        throw std::invalid_argument("supersample RGBA8 byte count does not match layout");
    }

    std::vector<std::uint8_t> result(
        RgbaByteCount(layout.logical_width, layout.logical_height));
    const auto samples = layout.factor * layout.factor;
    for (std::uint32_t y = 0; y < layout.logical_height; ++y) {
        for (std::uint32_t x = 0; x < layout.logical_width; ++x) {
            for (std::size_t channel = 0; channel < kRgbaChannels; ++channel) {
                std::uint32_t sum{};
                for (std::uint32_t sample_y = 0; sample_y < layout.factor; ++sample_y) {
                    for (std::uint32_t sample_x = 0; sample_x < layout.factor; ++sample_x) {
                        const auto source_x = x * layout.factor + sample_x;
                        const auto source_y = y * layout.factor + sample_y;
                        const auto source =
                            (static_cast<std::size_t>(source_y) * layout.render_width +
                             source_x) * kRgbaChannels + channel;
                        sum += pixels[source];
                    }
                }
                const auto destination =
                    (static_cast<std::size_t>(y) * layout.logical_width + x) *
                    kRgbaChannels + channel;
                result[destination] = static_cast<std::uint8_t>(
                    (sum + samples / 2U) / samples);
            }
        }
    }
    return result;
}

std::vector<std::uint8_t> ResolveSupersampledRgba8(
    std::vector<std::uint8_t>&& pixels,
    const SupersampleLayout& layout) {
    if (MakeSupersampleLayout(layout.logical_width, layout.logical_height,
                              layout.factor) != layout) {
        throw std::invalid_argument("supersample layout is inconsistent");
    }
    if (pixels.size() !=
        RgbaByteCount(layout.render_width, layout.render_height)) {
        throw std::invalid_argument(
            "supersample RGBA8 byte count does not match layout");
    }
    if (layout.factor == 1U) return std::move(pixels);
    return ResolveSupersampledRgba8(
        std::span<const std::uint8_t>{pixels}, layout);
}

}  // namespace ogplay::gles
