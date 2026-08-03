#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ogplay::core {

struct ImageView {
    std::span<const std::uint8_t> rgba;
    std::size_t width{};
    std::size_t height{};
};

struct FrameDifference {
    double pixel_difference_ratio{};
    std::uint32_t perceptual_hash_distance{};
};

[[nodiscard]] std::uint64_t AverageHash(ImageView image);
[[nodiscard]] FrameDifference CompareFrames(ImageView actual,
                                            ImageView expected,
                                            std::uint8_t channel_tolerance = 0);

}  // namespace ogplay::core

