#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <stdexcept>
#include <string>

#include "ogplay/core/frame_compare.h"
#include "ogplay/core/software_surface.h"
#include "ogplay/gles/angle_frame.h"
#include "ogplay/hal/host_environment.h"
#include "m4_exit_environment.h"

namespace {

#if defined(_WIN32)
constexpr ogplay::gles::AngleBackend kNativeHardwareBackend{
    ogplay::gles::AngleRenderer::d3d11,
    ogplay::gles::AngleDevice::hardware};
#elif defined(__APPLE__)
constexpr ogplay::gles::AngleBackend kNativeHardwareBackend{
    ogplay::gles::AngleRenderer::metal,
    ogplay::gles::AngleDevice::hardware};
#else
constexpr ogplay::gles::AngleBackend kNativeHardwareBackend{
    ogplay::gles::AngleRenderer::vulkan,
    ogplay::gles::AngleDevice::hardware};
#endif

void CheckAngleGoldenFrame(const ogplay::gles::AngleBackend backend) {
    constexpr std::uint32_t width = 8;
    constexpr std::uint32_t height = 8;
    auto frame = ogplay::gles::AngleFrame::CreatePbuffer(backend, width, height);
    CHECK_THROWS_AS(frame.Scissor(0, 0, -1, 1), std::invalid_argument);
    frame.Viewport(0, 0, width, height);
    frame.ClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    frame.Clear(0x00004000U);
    frame.SetScissorEnabled(true);
    frame.Scissor(1, 1, 3, 2);
    frame.ClearColor(1.0F, 0.0F, 0.0F, 1.0F);
    frame.Clear(0x00004000U);
    frame.SetScissorEnabled(false);
    const auto actual_pixels = frame.ReadRgba8();

    ogplay::core::SoftwareSurface expected(width, height);
    expected.Clear({.alpha = 255});
    expected.FillRect(1, 5, 3, 2, {.red = 255, .alpha = 255});
    const ogplay::core::ImageView actual{actual_pixels, width, height};
    const auto difference = ogplay::core::CompareFrames(actual, expected.View());
    CHECK(difference.pixel_difference_ratio == 0.0);
    CHECK(difference.perceptual_hash_distance == 0);
    CHECK(frame.Info().clear_count == 2);
    CHECK(frame.Info().readback_count == 1);
}

}  // namespace

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

TEST_CASE("ANGLE readback matches a top-left software golden frame") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) {
        CHECK_FALSE_MESSAGE(ogplay::tests::M4ExitVerificationRequired(),
                            "strict M4 exit requires native ANGLE EGL");
        return;
    }
    CheckAngleGoldenFrame(kNativeHardwareBackend);
}

TEST_CASE("ANGLE SwiftShader golden follows the verified SDK capability") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) {
        CHECK_FALSE_MESSAGE(ogplay::tests::M4ExitVerificationRequired(),
                            "strict M4 exit requires native ANGLE EGL");
        return;
    }
    const ogplay::gles::AngleBackend backend{
        ogplay::gles::AngleRenderer::vulkan,
        ogplay::gles::AngleDevice::swiftshader};
#if OGPLAY_ANGLE_HAS_SWIFTSHADER
    CheckAngleGoldenFrame(backend);
#else
    CHECK_THROWS_AS(
        ogplay::gles::AngleFrame::CreatePbuffer(backend, 8, 8),
        ogplay::gles::EglLifecycleError);
#endif
}

TEST_CASE("ANGLE SwiftShader initialization preserves backend isolation") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) {
        CHECK_FALSE_MESSAGE(ogplay::tests::M4ExitVerificationRequired(),
                            "strict M4 exit requires native ANGLE EGL");
        return;
    }
#if OGPLAY_ANGLE_HAS_SWIFTSHADER
    const auto initial_driver_files =
        ogplay::hal::HostEnvironmentValue("VK_DRIVER_FILES");
    const auto initial_icd_filenames =
        ogplay::hal::HostEnvironmentValue("VK_ICD_FILENAMES");
    const std::array conflicting_environment{
        ogplay::hal::HostEnvironmentOverride{
            "VK_DRIVER_FILES",
            std::string("ogplay-invalid-driver-files.json")},
        ogplay::hal::HostEnvironmentOverride{
            "VK_ICD_FILENAMES",
            std::string("ogplay-invalid-icd-filenames.json")},
    };
    {
        ogplay::hal::ScopedHostEnvironment conflicting_scope(
            conflicting_environment);
        {
            auto frame = ogplay::gles::AngleFrame::CreatePbuffer(
                {ogplay::gles::AngleRenderer::vulkan,
                 ogplay::gles::AngleDevice::swiftshader},
                4, 4);
            CHECK(frame.GetString(0x1F01U).find("SwiftShader") !=
                  std::string::npos);
        }
        CHECK(ogplay::hal::HostEnvironmentValue("VK_DRIVER_FILES") ==
              "ogplay-invalid-driver-files.json");
        CHECK(ogplay::hal::HostEnvironmentValue("VK_ICD_FILENAMES") ==
              "ogplay-invalid-icd-filenames.json");
    }
    const std::array clean_environment{
        ogplay::hal::HostEnvironmentOverride{"VK_DRIVER_FILES", std::nullopt},
        ogplay::hal::HostEnvironmentOverride{"VK_ICD_FILENAMES", std::nullopt},
    };
    {
        ogplay::hal::ScopedHostEnvironment clean_scope(clean_environment);
        {
            auto frame = ogplay::gles::AngleFrame::CreatePbuffer(
                {ogplay::gles::AngleRenderer::vulkan,
                 ogplay::gles::AngleDevice::swiftshader},
                4, 4);
            CHECK(frame.GetString(0x1F01U).find("SwiftShader") !=
                  std::string::npos);
            CHECK_FALSE(
                ogplay::hal::HostEnvironmentValue("VK_DRIVER_FILES")
                    .has_value());
            CHECK_FALSE(
                ogplay::hal::HostEnvironmentValue("VK_ICD_FILENAMES")
                    .has_value());
        }
        {
            auto frame = ogplay::gles::AngleFrame::CreatePbuffer(
                kNativeHardwareBackend, 4, 4);
            CHECK(frame.GetString(0x1F01U).find("SwiftShader") ==
                  std::string::npos);
        }
    }
    CHECK(ogplay::hal::HostEnvironmentValue("VK_DRIVER_FILES") ==
          initial_driver_files);
    CHECK(ogplay::hal::HostEnvironmentValue("VK_ICD_FILENAMES") ==
          initial_icd_filenames);
#else
    CHECK_THROWS_AS(
        ogplay::gles::AngleFrame::CreatePbuffer(
            {ogplay::gles::AngleRenderer::vulkan,
             ogplay::gles::AngleDevice::swiftshader},
            4, 4),
        ogplay::gles::EglLifecycleError);
#endif
}
