#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace ogplay::gles {

struct SupersampleLayout final {
    std::uint32_t logical_width{};
    std::uint32_t logical_height{};
    std::uint32_t render_width{};
    std::uint32_t render_height{};
    std::uint32_t factor{1};

    bool operator==(const SupersampleLayout&) const = default;
};

[[nodiscard]] SupersampleLayout MakeSupersampleLayout(
    std::uint32_t logical_width, std::uint32_t logical_height,
    std::uint32_t factor);

[[nodiscard]] std::vector<std::uint8_t> ResolveSupersampledRgba8(
    std::span<const std::uint8_t> pixels, const SupersampleLayout& layout);

}  // namespace ogplay::gles
