#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ogplay/gles/supersample.h"

namespace {

void FillBlock(std::vector<std::uint8_t>& pixels,
               const std::uint32_t block_x, const std::uint32_t block_y,
               const std::array<std::uint8_t, 4>& color) {
    constexpr std::uint32_t width = 4;
    for (std::uint32_t y = block_y * 2U; y < block_y * 2U + 2U; ++y) {
        for (std::uint32_t x = block_x * 2U; x < block_x * 2U + 2U; ++x) {
            const auto offset = (y * width + x) * 4U;
            for (std::size_t channel = 0; channel < color.size(); ++channel) {
                pixels[offset + channel] = color[channel];
            }
        }
    }
}

}  // namespace

TEST_CASE("supersample layout validates factors and dimension overflow") {
    const auto layout = ogplay::gles::MakeSupersampleLayout(640, 360, 3);
    CHECK(layout.logical_width == 640);
    CHECK(layout.logical_height == 360);
    CHECK(layout.render_width == 1920);
    CHECK(layout.render_height == 1080);
    CHECK(layout.factor == 3);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::gles::MakeSupersampleLayout(1, 1, 0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::gles::MakeSupersampleLayout(1, 1, 5)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::gles::MakeSupersampleLayout(
            std::numeric_limits<std::uint32_t>::max(), 1, 2)),
        std::overflow_error);
}

TEST_CASE("RGBA8 supersample resolve preserves block colors and orientation") {
    const auto layout = ogplay::gles::MakeSupersampleLayout(2, 2, 2);
    std::vector<std::uint8_t> pixels(4U * 4U * 4U);
    FillBlock(pixels, 0, 0, {255, 0, 0, 255});
    FillBlock(pixels, 1, 0, {0, 255, 0, 255});
    FillBlock(pixels, 0, 1, {0, 0, 255, 255});
    FillBlock(pixels, 1, 1, {255, 255, 255, 255});

    const auto resolved = ogplay::gles::ResolveSupersampledRgba8(pixels, layout);
    const std::vector<std::uint8_t> expected{
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255,
    };
    CHECK(resolved == expected);
}

TEST_CASE("RGBA8 one-times resolve retains the owned readback storage") {
    const auto layout = ogplay::gles::MakeSupersampleLayout(2, 2, 1);
    std::vector<std::uint8_t> pixels(2U * 2U * 4U, 37U);
    const auto* storage = pixels.data();
    const auto resolved = ogplay::gles::ResolveSupersampledRgba8(
        std::move(pixels), layout);
    CHECK(resolved.data() == storage);
    CHECK(resolved == std::vector<std::uint8_t>(2U * 2U * 4U, 37U));
}

TEST_CASE("RGBA8 supersample resolve rounds averages and rejects inconsistent input") {
    const auto layout = ogplay::gles::MakeSupersampleLayout(1, 1, 2);
    const std::array<std::uint8_t, 16> pixels{
        0, 0, 0, 255, 1, 0, 0, 255,
        2, 0, 0, 255, 3, 0, 0, 255,
    };
    const auto resolved = ogplay::gles::ResolveSupersampledRgba8(pixels, layout);
    REQUIRE(resolved.size() == 4);
    CHECK(resolved[0] == 2);
    CHECK(resolved[1] == 0);
    CHECK(resolved[2] == 0);
    CHECK(resolved[3] == 255);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::gles::ResolveSupersampledRgba8(
            std::span{pixels}.first(15), layout)),
        std::invalid_argument);
    auto inconsistent = layout;
    inconsistent.render_width = 3;
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::gles::ResolveSupersampledRgba8(
            pixels, inconsistent)),
        std::invalid_argument);
}
