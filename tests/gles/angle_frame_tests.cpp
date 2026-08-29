#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>

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
    frame.ClearStencil(3);
    frame.DepthRange(0.25F, 0.75F);
    frame.LineWidth(1.0F);
    frame.PolygonOffset(1.0F, 2.0F);
    frame.StencilFunction(0x0202U, 2, 0x7FU);
    frame.StencilMask(0x3FU);
    frame.StencilOperation(0x1E00U, 0x1E01U, 0x1E02U);
    frame.Clear(0x00004000U);
    const auto depth_bits = frame.GetIntegers(0x0D56U, 1U);
    const auto stencil_bits = frame.GetIntegers(0x0D57U, 1U);
    constexpr std::uint32_t kExtensions = 0x1F03U;
    constexpr std::uint32_t kPackReverseRowOrderAngle = 0x93A4U;
    const auto has_reverse_pack = frame.GetString(kExtensions).find(
        "GL_ANGLE_pack_reverse_row_order") != std::string::npos;
    if (has_reverse_pack) frame.PixelStore(kPackReverseRowOrderAngle, 0);
    const auto pixels = frame.ReadRgba8();

    REQUIRE(depth_bits.size() == 1U);
    REQUIRE(stencil_bits.size() == 1U);
    CHECK(depth_bits.front() >= 24);
    CHECK(stencil_bits.front() >= 8);
    CHECK(frame.GetIntegers(0x0B91U, 1U).front() == 3);
    CHECK(frame.GetIntegers(0x0B92U, 1U).front() == 0x0202);
    CHECK(frame.GetIntegers(0x0B93U, 1U).front() == 0x7F);
    CHECK(frame.GetIntegers(0x0B98U, 1U).front() == 0x3F);
    CHECK(frame.GetIntegers(0x0B94U, 1U).front() == 0x1E00);
    CHECK(frame.GetIntegers(0x0B95U, 1U).front() == 0x1E01);
    CHECK(frame.GetIntegers(0x0B96U, 1U).front() == 0x1E02);
    REQUIRE(pixels.size() == 4U * 3U * 4U);
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4U) {
        CHECK(pixels[offset] == doctest::Approx(64).epsilon(0.02));
        CHECK(pixels[offset + 1U] == doctest::Approx(128).epsilon(0.02));
        CHECK(pixels[offset + 2U] == doctest::Approx(191).epsilon(0.02));
        CHECK(pixels[offset + 3U] == 255);
    }
    CHECK(frame.Info().clear_count == 1);
    CHECK(frame.Info().readback_count == 1);
    if (has_reverse_pack) {
        const auto reverse_pack = frame.GetIntegers(kPackReverseRowOrderAngle, 1U);
        REQUIRE(reverse_pack.size() == 1U);
        CHECK(reverse_pack.front() == 0);
    }
}

TEST_CASE("ANGLE uniform discovery accepts OES texture 3D samplers") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    auto frame = ogplay::gles::AngleFrame::CreatePbuffer(
        {kNativeRenderer, ogplay::gles::AngleDevice::hardware}, 4, 3);
    if (frame.GetString(0x1F03U).find("GL_OES_texture_3D") ==
        std::string::npos) {
        return;
    }
    const auto vertex = frame.CreateShader(0x8B31U);
    const auto fragment = frame.CreateShader(0x8B30U);
    const std::string vertex_source =
        "attribute vec4 position; void main(){gl_Position=position;}";
    const std::string fragment_source =
        "#extension GL_OES_texture_3D : require\n"
        "precision mediump float; uniform lowp sampler3D texture; "
        "void main(){gl_FragColor=texture3D(texture,vec3(0.0));}";
    frame.ShaderSource(vertex, {&vertex_source, 1U});
    frame.ShaderSource(fragment, {&fragment_source, 1U});
    frame.CompileShader(vertex);
    frame.CompileShader(fragment);
    REQUIRE(frame.GetShaderParameter(vertex, 0x8B81U) != 0);
    INFO(frame.GetShaderInfoLog(fragment));
    REQUIRE(frame.GetShaderParameter(fragment, 0x8B81U) != 0);
    const auto program = frame.CreateProgram();
    frame.AttachShader(program, vertex);
    frame.AttachShader(program, fragment);
    frame.LinkProgram(program);
    REQUIRE(frame.GetProgramParameter(program, 0x8B82U) != 0);
    const auto uniforms = frame.DiscoverUniformValueCounts(program);
    REQUIRE(uniforms.size() == 1U);
    CHECK(uniforms.front().value_count == 1U);
    CHECK(uniforms.front().location ==
          frame.GetUniformLocation(program, "texture"));
}
