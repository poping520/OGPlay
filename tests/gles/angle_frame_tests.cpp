#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

#include "ogplay/gles/angle_frame.h"

namespace {

#if defined(_WIN32)
constexpr ogplay::gles::AngleRenderer kNativeRenderer =
    ogplay::gles::AngleRenderer::d3d11;
#elif defined(__APPLE__)
constexpr ogplay::gles::AngleRenderer kNativeRenderer =
    ogplay::gles::AngleRenderer::metal;
#else
constexpr ogplay::gles::AngleRenderer kNativeRenderer =
    ogplay::gles::AngleRenderer::vulkan;
#endif

}  // namespace

TEST_CASE("ANGLE frame clears and reads back an exact GLES2 pbuffer") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) {
        CHECK_THROWS_AS(ogplay::gles::AngleFrame::CreatePbuffer(
                            {kNativeRenderer,
                             ogplay::gles::AngleDevice::hardware}, 4, 3),
                        ogplay::gles::EglLifecycleError);
        return;
    }

    auto frame = ogplay::gles::AngleFrame::CreatePbuffer(
        {kNativeRenderer,
         ogplay::gles::AngleDevice::hardware}, 4, 3);
    frame.Viewport(0, 0, 4, 3);
    frame.ClearColor(0.25F, 0.5F, 0.75F, 1.0F);
    frame.Clear(0x00004000U);
    const auto depth_bits = frame.GetIntegers(0x0D56U, 1U);
    const auto stencil_bits = frame.GetIntegers(0x0D57U, 1U);
    const auto pixels = frame.ReadRgba8();

    REQUIRE(depth_bits.size() == 1U);
    REQUIRE(stencil_bits.size() == 1U);
    CHECK(depth_bits.front() >= 24);
    CHECK(stencil_bits.front() >= 8);
    REQUIRE(pixels.size() == 4U * 3U * 4U);
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4U) {
        CHECK(pixels[offset] == doctest::Approx(64).epsilon(0.02));
        CHECK(pixels[offset + 1U] == doctest::Approx(128).epsilon(0.02));
        CHECK(pixels[offset + 2U] == doctest::Approx(191).epsilon(0.02));
        CHECK(pixels[offset + 3U] == 255);
    }
    CHECK(frame.Info().clear_count == 1);
    CHECK(frame.Info().readback_count == 1);
}
