#pragma once

#include "include/core/SkBitmap.h"

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace ogplay::runtime::raster {
struct DecodeOptions final {
    int config{5};
    int sample{1};
    double scale{1};
    bool premultiplied{true};
    bool only_bounds{};
};

struct DecodedBitmap final {
    SkBitmap pixels;
    int width{}, height{};
    int config{};
    const char* mime{};
    std::vector<std::byte> nine_patch;
    std::optional<std::array<int, 4>> layout_bounds;
    std::array<int, 4> padding{-1, -1, -1, -1};
};

[[nodiscard]] std::optional<DecodedBitmap> DecodeBitmap(std::span<const std::byte> bytes, const DecodeOptions& options);
}  // namespace ogplay::runtime::raster
