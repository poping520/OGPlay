#include <doctest/doctest.h>

#include <array>
#include <stdexcept>

#include "ogplay/core/frame_compare.h"
#include "ogplay/core/software_surface.h"

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

TEST_CASE("software surface produces a repeatable headless golden frame") {
    ogplay::core::SoftwareSurface actual(8, 8);
    actual.Clear({.red = 8, .green = 16, .blue = 24});
    actual.FillRect(2, 2, 4, 4, {.red = 220, .green = 120, .blue = 20});

    ogplay::core::SoftwareSurface expected(8, 8);
    expected.Clear({.red = 8, .green = 16, .blue = 24});
    expected.FillRect(2, 2, 4, 4, {.red = 220, .green = 120, .blue = 20});

    const auto exact = ogplay::core::CompareFrames(actual.View(), expected.View());
    CHECK(exact.pixel_difference_ratio == 0.0);
    CHECK(exact.perceptual_hash_distance == 0);

    actual.FillRect(7, 7, 8, 8, {.red = 255});
    const auto changed = ogplay::core::CompareFrames(actual.View(), expected.View());
    CHECK(changed.pixel_difference_ratio == doctest::Approx(1.0 / 64.0));
}
