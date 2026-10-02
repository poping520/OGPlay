#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <array>

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

TEST_CASE("ANGLE bounded buffer read observes updates and preserves mappings and bindings") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    for (const int version : {2, 3}) {
        CAPTURE(version);
        auto frame = ogplay::gles::AngleFrame::CreatePbuffer(
            {kNativeRenderer, ogplay::gles::AngleDevice::hardware}, 4, 4, version);
        const auto buffers = frame.GenerateBuffers(2);
        frame.BindBuffer(0x8892U, buffers[0]);
        frame.BindBuffer(0x8893U, buffers[1]);
        frame.BufferData(0x8893U, 16, std::nullopt, 0x88E4U);
        std::array<std::byte, 4> bytes{std::byte{3}, std::byte{1}, std::byte{9}, std::byte{2}};
        frame.BufferSubData(0x8893U, 5, bytes);
        CHECK(frame.ReadBufferRange(0x8893U, 5, 4) == std::vector<std::byte>(bytes.begin(), bytes.end()));
        bytes[2] = std::byte{7};
        frame.BufferSubData(0x8893U, 5, bytes);
        CHECK(frame.ReadBufferRange(0x8893U, 5, 4)[2] == std::byte{7});
        CHECK(frame.GetIntegers(0x8894U, 1)[0] == static_cast<std::int32_t>(buffers[0]));
        CHECK(frame.GetIntegers(0x8895U, 1)[0] == static_cast<std::int32_t>(buffers[1]));
        CHECK(frame.GetBufferParameter(0x8893U, 0x88BCU) == 0);
        CHECK_THROWS_AS(static_cast<void>(frame.ReadBufferRange(0x8893U, 15, 2)), ogplay::gles::GlesApiError);
        CHECK_THROWS_AS(static_cast<void>(frame.ReadBufferRange(0x8893U, UINT32_MAX, 1)), ogplay::gles::GlesApiError);
        auto* mapped = frame.MapBufferOes(0x8893U, 0x88B9U);
        REQUIRE(mapped != nullptr);
        mapped[7] = std::byte{8};
        CHECK_THROWS_AS(static_cast<void>(frame.ReadBufferRange(0x8893U, 5, 4)), ogplay::gles::GlesApiError);
        CHECK(frame.GetBufferParameter(0x8893U, 0x88BCU) != 0);
        REQUIRE(frame.UnmapBufferOes(0x8893U));
        CHECK(frame.ReadBufferRange(0x8893U, 5, 4)[2] == std::byte{8});
        frame.ReleaseCurrent();
        auto shared = ogplay::gles::AngleFrame::CreatePbuffer(
            {kNativeRenderer, ogplay::gles::AngleDevice::hardware}, 4, 4, version, frame.NativeContext());
        shared.BindBuffer(0x8893U, buffers[1]);
        bytes[2] = std::byte{6};
        shared.BufferSubData(0x8893U, 5, bytes);
        shared.ReleaseCurrent();
        frame.BindCurrentOnCallingThread();
        CHECK(frame.ReadBufferRange(0x8893U, 5, 4)[2] == std::byte{6});
        frame.DeleteBuffers(buffers);
    }
}

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

TEST_CASE("ANGLE pbuffer contexts share resources but keep framebuffer content") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    const ogplay::gles::AngleBackend backend{
        kNativeRenderer, ogplay::gles::AngleDevice::hardware};
    auto first = ogplay::gles::AngleFrame::CreatePbuffer(backend, 4, 3);
    const auto textures = first.GenerateTextures(1U);
    REQUIRE(textures.size() == 1U);
    first.BindTexture(0x0DE1U, textures.front());
    first.ClearColor(1.0F, 0.0F, 0.0F, 1.0F);
    first.Clear(0x00004000U);
    first.ReleaseCurrent();

    auto second = ogplay::gles::AngleFrame::CreatePbuffer(
        backend, 2, 2, 2, first.NativeContext());
    CHECK(second.IsTexture(textures.front()));
    second.ClearColor(0.0F, 1.0F, 0.0F, 1.0F);
    second.Clear(0x00004000U);
    const auto green = second.ReadRgba8();
    second.ReleaseCurrent();

    first.BindCurrentOnCallingThread();
    const auto red = first.ReadRgba8();
    REQUIRE(red.size() == 4U * 3U * 4U);
    REQUIRE(green.size() == 2U * 2U * 4U);
    for (std::size_t offset = 0; offset < red.size(); offset += 4U) {
        CHECK(red[offset] == 255U);
        CHECK(red[offset + 1U] == 0U);
    }
    for (std::size_t offset = 0; offset < green.size(); offset += 4U) {
        CHECK(green[offset] == 0U);
        CHECK(green[offset + 1U] == 255U);
    }
}

TEST_CASE("ATC fallback samples real textures and preserves PBO and unpack state") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    for (const int version : {2, 3}) {
        CAPTURE(version);
        auto frame = ogplay::gles::AngleFrame::CreatePbuffer(
            {kNativeRenderer, ogplay::gles::AngleDevice::hardware}, 4, 4, version);
        std::array<std::byte, 16> block{};
        std::fill_n(block.begin(), 8, std::byte{0x88});
        block[9] = std::byte{0x7c}; // Uniform red, explicit alpha = 136.
        const auto tex = frame.GenerateTextures(1).front();
        frame.BindTexture(0x0de1, tex);
        frame.TextureParameter(0x0de1, 0x2801, 0x2600);
        frame.TextureParameter(0x0de1, 0x2800, 0x2600);
        frame.PixelStore(0x0cf5, 8);
        frame.CompressedTextureImage2D(0x0de1, 0, 0x8c93, 4, 4, 0, block);
        frame.CompressedTextureImage2D(0x0de1, 1, 0x8c93, 2, 2, 0, block);
        CHECK(frame.GetIntegers(0x0cf5, 1).front() == 8);
        if (version == 3) {
            const auto pbo = frame.GenerateBuffers(1).front();
            frame.BindBuffer(0x88ec, pbo);
            std::array<std::byte, 32> storage{};
            std::copy(block.begin(), block.end(), storage.begin() + 16);
            frame.BufferData(0x88ec, storage.size(), std::span<const std::byte>(storage), 0x88e4);
            frame.PixelStore(0x0cf2, 9);
            frame.PixelStore(0x0cf4, 3);
            const std::array<std::uint32_t, 8> args{0x0de1,0,0x8c93,4,4,0,16,16};
            frame.TransferPixelBuffer(ogplay::gles::AngleFrame::PixelBufferOperation::compressed2d, args);
            CHECK(frame.BoundBuffer(0x88ec) == pbo);
            CHECK(frame.GetIntegers(0x0cf2,1).front() == 9);
            CHECK(frame.GetIntegers(0x0cf4,1).front() == 3);
            CHECK(frame.GetIntegers(0x0cf5,1).front() == 8);
            auto invalid = args;invalid[7]=24;
            CHECK_THROWS_AS(frame.TransferPixelBuffer(ogplay::gles::AngleFrame::PixelBufferOperation::compressed2d, invalid), ogplay::gles::GlesApiError);
            CHECK(frame.BoundBuffer(0x88ec) == pbo);
        }
        const auto vs = frame.CreateShader(0x8b31), fs = frame.CreateShader(0x8b30);
        const std::string vsrc = "attribute vec2 p;void main(){gl_Position=vec4(p,0.,1.);}";
        const std::string fsrc = "precision mediump float;uniform sampler2D t;void main(){gl_FragColor=texture2D(t,vec2(.5));}";
        frame.ShaderSource(vs, {&vsrc,1});frame.ShaderSource(fs,{&fsrc,1});
        frame.CompileShader(vs);frame.CompileShader(fs);
        const auto program = frame.CreateProgram();frame.AttachShader(program,vs);frame.AttachShader(program,fs);
        frame.LinkProgram(program);REQUIRE(frame.GetProgramParameter(program,0x8b82)==1);
        frame.UseProgram(program);frame.Uniform1i(frame.GetUniformLocation(program,"t"),0);
        const std::array<float,6> triangle{-1,-1,3,-1,-1,3};
        const auto vbo = frame.GenerateBuffers(1).front();frame.BindBuffer(0x8892,vbo);
        frame.BufferData(0x8892,sizeof(triangle),std::as_bytes(std::span(triangle)),0x88e4);
        const auto attrib = static_cast<std::uint32_t>(frame.GetAttribLocation(program,"p"));
        frame.VertexAttributePointer(attrib,2,0x1406,false,0,0);frame.SetVertexAttributeEnabled(attrib,true);
        frame.Viewport(0,0,4,4);frame.DrawArrays(4,0,3);
        const auto pixels = frame.ReadRgba8();
        REQUIRE(pixels.size()==64);
        for(std::size_t i=0;i<pixels.size();i+=4) {
            CHECK(pixels[i]==255);CHECK(pixels[i+1]==0);CHECK(pixels[i+2]==0);CHECK(pixels[i+3]==136);
        }
        if (version == 3) frame.BindBuffer(0x88ec,0);
        const auto cube = frame.GenerateTextures(1).front();
        frame.BindTexture(0x8513,cube);
        for(std::uint32_t face=0x8515;face<=0x851a;++face)
            frame.CompressedTextureImage2D(face,0,0x8c93,4,4,0,block);
        CHECK(frame.GetError()==0U);
        frame.BindTexture(0x0de1,tex);
        const auto formats = frame.CompressedTextureFormats();
        CHECK(frame.GetIntegers(0x86a2,1).front()==static_cast<std::int32_t>(formats.size()));
        CHECK(frame.StateQueryCount(0x86a3)==formats.size());
        CHECK(frame.GetIntegers(0x86a3,formats.size())==formats);
        for(const auto format : {0x8c92,0x8c93,0x87ee})
            CHECK(std::count(formats.begin(),formats.end(),format)==1);
        CHECK(frame.GetString(0x1f03).find("GL_AMD_compressed_ATC_texture")!=std::string::npos);
        CHECK_THROWS_AS(frame.CompressedTextureImage2D(0x0de1,0,0x8c93,4,4,0,std::span(block).first(15)),ogplay::gles::GlesApiError);
        CHECK_THROWS_AS(frame.CompressedTextureImage2D(0x0de1,0,0x8c93,4,4,1,block),ogplay::gles::GlesApiError);
        try { frame.CompressedTextureSubImage2D(0x0de1,0,0,0,4,4,0x8c93,block);FAIL("ATC subimage succeeded"); }
        catch(const ogplay::gles::GlesApiError& error) { CHECK(error.Code()==0x0502); }
    }
}
