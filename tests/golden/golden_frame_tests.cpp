#include <doctest/doctest.h>

#include <array>
#include <stdexcept>

#include "ogplay/core/frame_compare.h"

TEST_CASE("software frame comparison is deterministic without a GPU") {
    std::array<std::uint8_t, 8U * 8U * 4U> baseline{};
    for (std::size_t pixel = 0; pixel < 64U; ++pixel) {
        baseline[pixel * 4U] = static_cast<std::uint8_t>(pixel * 4U);
        baseline[pixel * 4U + 3U] = 255;
    }
    auto changed = baseline;
    changed[0] = 255;

    const ogplay::core::ImageView expected{baseline, 8, 8};
    const ogplay::core::ImageView actual{changed, 8, 8};
    const auto difference = ogplay::core::CompareFrames(actual, expected);

    CHECK(difference.pixel_difference_ratio == doctest::Approx(1.0 / 64.0));
    CHECK(difference.perceptual_hash_distance == 2);
    CHECK(ogplay::core::AverageHash(expected) == ogplay::core::AverageHash(expected));
}

TEST_CASE("frame comparison rejects invalid dimensions") {
    const std::array<std::uint8_t, 4> pixel{0, 0, 0, 255};
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::core::CompareFrames({pixel, 1, 1}, {pixel, 2, 1})),
        std::invalid_argument);
}

TEST_CASE("average hash supports images smaller than eight pixels") {
    const std::array<std::uint8_t, 4> pixel{12, 34, 56, 255};
    const ogplay::core::ImageView image{pixel, 1, 1};
    CHECK(ogplay::core::AverageHash(image) == ~std::uint64_t{0});
    CHECK(ogplay::core::CompareFrames(image, image).pixel_difference_ratio == 0.0);
}
