#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

#include "ogplay/gles/angle_frame.h"

TEST_CASE("ANGLE frame clears and reads back an exact GLES2 pbuffer") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) {
        CHECK_THROWS_AS(ogplay::gles::AngleFrame::CreatePbuffer(
                            {ogplay::gles::AngleRenderer::d3d11,
                             ogplay::gles::AngleDevice::hardware}, 4, 3),
                        ogplay::gles::EglLifecycleError);
        return;
    }

    auto frame = ogplay::gles::AngleFrame::CreatePbuffer(
        {ogplay::gles::AngleRenderer::d3d11,
         ogplay::gles::AngleDevice::hardware}, 4, 3);
    frame.Viewport(0, 0, 4, 3);
    frame.ClearColor(0.25F, 0.5F, 0.75F, 1.0F);
    frame.Clear(0x00004000U);
    const auto pixels = frame.ReadRgba8();

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
