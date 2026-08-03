#include "ogplay/core/frame_compare.h"

#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace ogplay::core {
namespace {

void Validate(const ImageView image) {
    if (image.width == 0 || image.height == 0) {
        throw std::invalid_argument("frame dimensions must be non-zero");
    }
    if (image.width > (static_cast<std::size_t>(-1) / image.height) / 4U ||
        image.rgba.size() != image.width * image.height * 4U) {
        throw std::invalid_argument("RGBA frame size does not match its dimensions");
    }
}

std::uint8_t Luminance(const std::uint8_t* pixel) {
    const auto weighted = static_cast<std::uint32_t>(pixel[0]) * 299U +
                          static_cast<std::uint32_t>(pixel[1]) * 587U +
                          static_cast<std::uint32_t>(pixel[2]) * 114U;
    return static_cast<std::uint8_t>(weighted / 1000U);
}

}  // namespace

std::uint64_t AverageHash(const ImageView image) {
    Validate(image);
    std::array<std::uint32_t, 64> cells{};
    for (std::size_t cell_y = 0; cell_y < 8U; ++cell_y) {
        for (std::size_t cell_x = 0; cell_x < 8U; ++cell_x) {
            const auto x = cell_x * image.width / 8U;
            const auto y = cell_y * image.height / 8U;
            const auto cell = cell_y * 8U + cell_x;
            cells[cell] = Luminance(image.rgba.data() + (y * image.width + x) * 4U);
        }
    }

    std::uint64_t total = 0;
    for (const auto luminance : cells) {
        total += luminance;
    }
    const auto average = total / cells.size();

    std::uint64_t hash = 0;
    for (std::size_t index = 0; index < cells.size(); ++index) {
        if (cells[index] >= average) {
            hash |= std::uint64_t{1} << index;
        }
    }
    return hash;
}

FrameDifference CompareFrames(const ImageView actual,
                              const ImageView expected,
                              const std::uint8_t channel_tolerance) {
    Validate(actual);
    Validate(expected);
    if (actual.width != expected.width || actual.height != expected.height) {
        throw std::invalid_argument("frame dimensions differ");
    }

    std::size_t changed_pixels = 0;
    const auto pixels = actual.width * actual.height;
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        bool changed = false;
        for (std::size_t channel = 0; channel < 4U; ++channel) {
            const auto index = pixel * 4U + channel;
            const auto delta = std::abs(static_cast<int>(actual.rgba[index]) -
                                        static_cast<int>(expected.rgba[index]));
            changed = changed || delta > channel_tolerance;
        }
        changed_pixels += changed ? 1U : 0U;
    }

    return {
        .pixel_difference_ratio = static_cast<double>(changed_pixels) /
                                  static_cast<double>(pixels),
        .perceptual_hash_distance = static_cast<std::uint32_t>(
            std::popcount(AverageHash(actual) ^ AverageHash(expected))),
    };
}

}  // namespace ogplay::core
