#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include "ogplay/hal/clock.h"
#include <future>
#include "ogplay/runtime/vfs/vfs.h"
#include <unordered_map>
#include <utility>
#include <vector>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/cpu/dynarmic.h"
#include "ogplay/core/logger.h"
#include "ogplay/gles/egl_lifecycle.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/gles_transfer_state.h"
#include "ogplay/memory/bus.h"
#include "ogplay/loader/link_namespace.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/modules/gles1/gles1_dispatch.h"
#include "runtime/boundary/modules/gles1/gles1_draw.h"
#include "runtime/boundary/modules/gles1/gles1_query.h"
#include "runtime/boundary/modules/gles1/gles1_support.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "runtime/boundary/services/graphics_dispatch.h"
#include "runtime/boundary/modules/opensles/opensles_abi.h"

#include "android_boundary_fixture.h"

using namespace ogplay::test::android_boundary;

TEST_CASE("GLES1 shade model state validates and resets") {
    ogplay::runtime::detail::AndroidBoundaryGles1State state;
    CHECK(state.ShadeModel() ==
          ogplay::runtime::detail::kGles1SmoothShadeModel);
    state.SetShadeModel(ogplay::runtime::detail::kGles1FlatShadeModel);
    CHECK(state.ShadeModel() ==
          ogplay::runtime::detail::kGles1FlatShadeModel);
    state.SetShadeModel(ogplay::runtime::detail::kGles1SmoothShadeModel);
    CHECK_THROWS_WITH_AS(
        state.SetShadeModel(0U),
        "glShadeModel mode must be GL_FLAT or GL_SMOOTH",
        std::invalid_argument);
    auto transfer = state.TransferState();
    transfer.BindBuffer(0x8892U, 7U);
    state.SetTransferState(std::move(transfer));
    CHECK(state.TransferState().Snapshot().array_buffer == 7U);
    state.SetHint(0x0C50U, 0x1102U);
    CHECK(state.Hint(0x0C50U) == 0x1102U);
    CHECK_THROWS_WITH_AS(
        state.SetHint(0U, 0x1100U),
        "glHint target is invalid for GLES1", std::invalid_argument);
    state.SetCapability(0x0DE1U, true);
    CHECK(state.Capability(0x0DE1U));
    state.BindTexture(0x0DE1U, 7U);
    CHECK_FALSE(state.TextureBaseFormat(0x0DE1U).has_value());
    state.SetTextureBaseFormat(0x0DE1U, 0x1908U);
    REQUIRE(state.TextureBaseFormat(0x0DE1U).has_value());
    CHECK(*state.TextureBaseFormat(0x0DE1U) == 0x1908U);
    CHECK(state.EnabledTextureUnits() == std::vector<std::uint32_t>{0x84C0U});
    CHECK_FALSE(state.GenerateMipmapEnabled(0x0DE1U));
    state.SetGenerateMipmap(0x0DE1U, true);
    CHECK(state.GenerateMipmapEnabled(0x0DE1U));
    state.SetActiveTexture(0x84C1U);
    CHECK_FALSE(state.Capability(0x0DE1U));
    CHECK(state.Capability(0x84C0U, 0x0DE1U));
    REQUIRE(state.TextureBaseFormat(0x84C0U, 0x0DE1U).has_value());
    CHECK(*state.TextureBaseFormat(0x84C0U, 0x0DE1U) == 0x1908U);
    CHECK(state.EnabledTextureUnits() == std::vector<std::uint32_t>{0x84C0U});
    state.BindTexture(0x0DE1U, 8U);
    CHECK_FALSE(state.TextureBaseFormat(0x0DE1U).has_value());
    CHECK_FALSE(state.GenerateMipmapEnabled(0x0DE1U));
    state.SetCapability(0x0DE1U, true);
    CHECK(state.Capability(0x0DE1U));
    CHECK(state.EnabledTextureUnits() ==
          std::vector<std::uint32_t>{0x84C0U, 0x84C1U});
    const std::array deleted_textures{7U};
    state.DeleteTextures(deleted_textures);
    state.SetActiveTexture(0x84C0U);
    CHECK_FALSE(state.TextureBaseFormat(0x0DE1U).has_value());
    CHECK_FALSE(state.GenerateMipmapEnabled(0x0DE1U));
    CHECK_FALSE(state.TextureBaseFormat(0x0DE1U).has_value());
    CHECK_THROWS_WITH_AS(
        state.SetGenerateMipmap(0U, true),
        "GLES1 texture target must be GL_TEXTURE_2D, got 0x0",
        std::invalid_argument);
    CHECK_THROWS_WITH_AS(
        state.SetCapability(0U, true), "GLES1 capability is invalid",
        std::invalid_argument);
    // OES_texture_cube_map: bindings address the cube target while face
    // targets only appear on uploads and normalize onto the same object.
    state.BindTexture(0x8513U, 9U);
    CHECK(state.BoundTexture(0x8513U) == 9U);
    CHECK_THROWS_WITH_AS(
        state.BindTexture(0x8515U, 9U),
        "GLES1 texture target must be GL_TEXTURE_2D, got 0x8515",
        std::invalid_argument);
    state.SetTextureBaseFormat(0x8515U, 0x1908U);
    REQUIRE(state.TextureBaseFormat(0x84C0U, 0x8513U).has_value());
    CHECK(*state.TextureBaseFormat(0x84C0U, 0x8513U) == 0x1908U);
    state.SetGenerateMipmap(0x8513U, true);
    CHECK(state.GenerateMipmapEnabled(0x8513U));
    CHECK(state.GenerateMipmapEnabled(0x8515U));
    state.SetCapability(0x8513U, true);
    CHECK(state.Capability(0x84C0U, 0x8513U));
    CHECK_FALSE(state.Capability(0x84C1U, 0x8513U));
    state.Reset();
    CHECK(state.ShadeModel() ==
          ogplay::runtime::detail::kGles1SmoothShadeModel);
    CHECK(state.TransferState().Snapshot().array_buffer == 0U);
    CHECK(state.Hint(0x0C50U) ==
          ogplay::runtime::detail::kGles1DontCare);
    CHECK(state.ActiveTexture() == 0x84C0U);
    CHECK_FALSE(state.Capability(0x0DE1U));
    CHECK(state.EnabledTextureUnits().empty());
    CHECK_FALSE(state.GenerateMipmapEnabled(0x0DE1U));
}

TEST_CASE("GLES1 matrix state composes and bounds stacks") {
    ogplay::runtime::SharedGlState shared;
    ogplay::runtime::detail::AndroidBoundaryGles1MatrixState matrices(shared);
    CHECK(matrices.Mode() == ogplay::runtime::detail::kGles1Modelview);
    CHECK(matrices.StackDepth(ogplay::runtime::detail::kGles1Modelview) == 1U);
    CHECK(matrices.Current()[0] == 1.0F);
    CHECK(matrices.Current()[15] == 1.0F);

    matrices.SetMode(ogplay::runtime::detail::kGles1Projection);
    matrices.Translate(1.0F, 2.0F, 3.0F);
    CHECK(matrices.Current()[12] == 1.0F);
    CHECK(matrices.Current()[13] == 2.0F);
    CHECK(matrices.Current()[14] == 3.0F);
    matrices.Push();
    matrices.Rotate(90.0F, 0.0F, 0.0F, 1.0F);
    CHECK(matrices.Current()[0] == doctest::Approx(0.0F).epsilon(0.0001));
    CHECK(matrices.Current()[1] == doctest::Approx(1.0F).epsilon(0.0001));
    CHECK(matrices.Current()[4] == doctest::Approx(-1.0F).epsilon(0.0001));
    matrices.Pop();
    CHECK(matrices.Current()[12] == 1.0F);
    CHECK_THROWS_WITH_AS(matrices.Pop(), "GLES1 matrix stack underflow",
                         std::underflow_error);
    CHECK_THROWS_WITH_AS(
        matrices.SetMode(0U), "glMatrixMode mode is invalid for GLES1",
        std::invalid_argument);
    CHECK_THROWS_WITH_AS(
        matrices.Rotate(1.0F, 0.0F, 0.0F, 0.0F),
        "GLES1 rotation axis is invalid", std::invalid_argument);
    const std::array plane{1.0F, 0.0F, 0.0F, 0.0F};
    auto translated = ogplay::runtime::detail::Gles1IdentityMatrix();
    translated[12] = 2.0F;
    const auto eye_plane =
        ogplay::runtime::detail::Gles1TransformClipPlane(translated, plane);
    CHECK(eye_plane[0] == doctest::Approx(1.0F));
    CHECK(eye_plane[3] == doctest::Approx(-2.0F));
    const ogplay::runtime::detail::Gles1Matrix singular{};
    CHECK_THROWS_WITH_AS(
        ogplay::runtime::detail::Gles1TransformClipPlane(singular, plane),
        "GLES1 clip-plane modelview matrix is singular", std::invalid_argument);
    matrices.SetMode(ogplay::runtime::detail::kGles1Texture);
    shared.SetActiveTexture(0x84C0U);
    matrices.Translate(0.25F, 0.0F, 0.0F);
    shared.SetActiveTexture(0x84C1U);
    CHECK(matrices.Current()[12] == 0.0F);
    matrices.Translate(0.5F, 0.0F, 0.0F);
    CHECK(matrices.Current(ogplay::runtime::detail::kGles1Texture,
                           0x84C0U)[12] == 0.25F);
    CHECK(matrices.Current(ogplay::runtime::detail::kGles1Texture,
                           0x84C1U)[12] == 0.5F);
    CHECK_THROWS_WITH_AS(
        shared.SetActiveTexture(0U),
        "shared GL texture unit is outside GL_TEXTURE0..31",
        std::invalid_argument);
    const ogplay::runtime::detail::Gles1Matrix invalid{
        std::numeric_limits<float>::quiet_NaN()};
    CHECK_THROWS_WITH_AS(
        matrices.Load(invalid), "GLES1 matrix value must be finite",
        std::invalid_argument);
    matrices.Reset();
    for (std::size_t depth = 1; depth < 32; ++depth) matrices.Push();
    CHECK_THROWS_WITH_AS(matrices.Push(), "GLES1 matrix stack overflow",
                         std::overflow_error);
}

TEST_CASE("GLES1 legacy fixed state validates isolates and resets") {
    ogplay::runtime::detail::AndroidBoundaryGles1LegacyState state;
    CHECK(state.AlphaFunction() == 0x0207U);
    CHECK(state.AlphaReference() == 0.0F);
    CHECK(state.ClientActiveTexture() == 0x84C0U);
    CHECK(state.Color()[0] == 1.0F);
    CHECK(state.Normal()[2] == 1.0F);
    CHECK(state.ClipPlane(0x3000U)[0] == 0.0F);
    state.SetAlphaFunction(0x0201U, 2.0F);
    CHECK(state.AlphaFunction() == 0x0201U);
    CHECK(state.AlphaReference() == 1.0F);
    state.SetClientActiveTexture(0x84C1U);
    CHECK(state.ClientActiveTexture() == 0x84C1U);
    const std::array color{-1.0F, 0.25F, 0.5F, 2.0F};
    state.SetColor(color);
    CHECK(state.Color()[0] == 0.0F);
    CHECK(state.Color()[3] == 1.0F);
    const std::array normal{0.25F, 0.5F, 0.75F};
    state.SetNormal(normal);
    CHECK(state.Normal()[1] == 0.5F);
    const std::array clip_plane{1.0F, 0.0F, 0.0F, -0.25F};
    state.SetClipPlane(0x3005U, clip_plane);
    CHECK(state.ClipPlane(0x3005U)[3] == -0.25F);
    CHECK_THROWS_AS(state.SetClipPlane(0x3006U, clip_plane),
                    std::invalid_argument);
    const std::array environment_color{0.1F, 0.2F, 0.3F, 0.4F};
    state.SetTextureEnvironment(
        0x84C1U, ogplay::runtime::detail::kGles1TextureEnvironment,
        ogplay::runtime::detail::kGles1TextureEnvironmentColor,
        environment_color);
    CHECK(state.TextureEnvironment(
              0x84C1U,
              ogplay::runtime::detail::kGles1TextureEnvironmentColor)[2] ==
          doctest::Approx(0.3F));
    CHECK_THROWS_AS(state.SetClientActiveTexture(0U), std::invalid_argument);
    CHECK_THROWS_AS(state.SetAlphaFunction(0U, 0.0F), std::invalid_argument);
    state.Reset();
    CHECK(state.ClientActiveTexture() == 0x84C0U);
    CHECK(state.Normal()[2] == 1.0F);
    CHECK(state.ClipPlane(0x3005U)[3] == 0.0F);
    CHECK(state.TextureEnvironment(
              0x84C0U,
              ogplay::runtime::detail::kGles1TextureEnvironmentMode)[0] ==
          8448.0F);
}

TEST_CASE("GLES1 client array state validates texture units and resets") {
    ogplay::runtime::detail::AndroidBoundaryGles1DrawState state;
    const auto check_defaults = [&state] {
        const auto& vertex = state.Array(
            ogplay::runtime::detail::kGles1VertexArray, 0x84C0U);
        CHECK(vertex.size == 4);
        CHECK(vertex.type == 0x1406U);
        CHECK(vertex.stride == 0);
        CHECK(vertex.pointer == 0U);
        CHECK(vertex.buffer == 0U);
        CHECK_FALSE(vertex.enabled);
        const auto& normal = state.Array(
            ogplay::runtime::detail::kGles1NormalArray, 0x84C0U);
        CHECK(normal.size == 3);
        CHECK(normal.type == 0x1406U);
        const auto& matrix_index = state.Array(
            ogplay::runtime::detail::kGles1MatrixIndexArray, 0x84C0U);
        CHECK(matrix_index.size == 4);
        CHECK(matrix_index.type == 0x1401U);
        const auto& texture = state.Array(
            ogplay::runtime::detail::kGles1TextureCoordArray, 0x84DFU);
        CHECK(texture.size == 4);
        CHECK(texture.type == 0x1406U);
    };
    check_defaults();
    state.SetPointer(ogplay::runtime::detail::kGles1VertexArray, 0x84C0U,
                     3, 0x1406U, 0, 0x1000U, 0U);
    state.SetEnabled(ogplay::runtime::detail::kGles1VertexArray, 0x84C0U,
                     true);
    CHECK(state.Array(ogplay::runtime::detail::kGles1VertexArray,
                      0x84C0U).enabled);
    CHECK(state.Array(ogplay::runtime::detail::kGles1VertexArray,
                      0x84C0U).pointer == 0x1000U);
    state.SetPointer(ogplay::runtime::detail::kGles1TextureCoordArray,
                     0x84C1U, 2, 0x1406U, 8, 0x2000U, 7U);
    CHECK(state.Array(ogplay::runtime::detail::kGles1TextureCoordArray,
                      0x84C1U).buffer == 7U);
    const auto prepared = state.PreparePointer(
        ogplay::runtime::detail::kGles1VertexArray, 0x84C0U,
        2, 0x1406U, 8, 0x3000U, 9U);
    CHECK(state.Array(ogplay::runtime::detail::kGles1VertexArray,
                      0x84C0U).pointer == 0x1000U);
    CHECK(prepared.enabled);
    state.CommitPointer(ogplay::runtime::detail::kGles1VertexArray,
                        0x84C0U, prepared);
    CHECK(state.Array(ogplay::runtime::detail::kGles1VertexArray,
                      0x84C0U).pointer == 0x3000U);
    CHECK(state.Array(ogplay::runtime::detail::kGles1VertexArray,
                      0x84C0U).buffer == 9U);
    CHECK_THROWS_AS(
        state.SetPointer(ogplay::runtime::detail::kGles1ColorArray,
                         0x84C0U, 3, 0x1406U, 0, 0U, 0U),
        std::invalid_argument);
    CHECK_THROWS_AS(
        state.SetPointer(ogplay::runtime::detail::kGles1VertexArray,
                         0x84C0U, 3, 0U, 0, 0U, 0U),
        std::invalid_argument);
    state.SetCurrentPaletteMatrix(7U);
    CHECK(state.CurrentPaletteMatrix() == 7U);
    state.SetPointer(ogplay::runtime::detail::kGles1MatrixIndexArray,
                     0x84C0U, 4, 0x1401U, 0, 0x3000U, 0U);
    state.SetPointer(ogplay::runtime::detail::kGles1WeightArray,
                     0x84C0U, 4, 0x1406U, 0, 0x4000U, 0U);
    CHECK_THROWS_AS(state.SetCurrentPaletteMatrix(32U),
                    std::invalid_argument);
    state.Reset();
    CHECK(state.CurrentPaletteMatrix() == 0U);
    check_defaults();
}

TEST_CASE("GLES1 single-stage texture coordinate fallback is unique and optional") {
    using namespace ogplay::runtime::detail;
    AndroidBoundaryGles1DrawState compatible;
    compatible.SetEnabled(kGles1TextureCoordArray, 0x84C0U, true);
    CHECK(compatible.ResolveTextureCoordinateUnits(
              std::array<std::uint32_t, 1>{0x84DFU}) ==
          std::array<std::uint32_t, 2>{0x84C0U, 0U});

    compatible.SetEnabled(kGles1TextureCoordArray, 0x84DFU, true);
    CHECK(compatible.ResolveTextureCoordinateUnits(
              std::array<std::uint32_t, 1>{0x84DFU}) ==
          std::array<std::uint32_t, 2>{0x84DFU, 0U});
    compatible.SetEnabled(kGles1TextureCoordArray, 0x84DFU, false);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(compatible.ResolveTextureCoordinateUnits(
            std::array<std::uint32_t, 2>{0x84C1U, 0x84DFU})),
        "GLES1 multi-stage draw has no texture coordinate array for one "
        "sampled unit; coordinate arrays cannot be shared",
        std::runtime_error);

    AndroidBoundaryGles1DrawState strict(false);
    strict.SetEnabled(kGles1TextureCoordArray, 0x84C0U, true);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(strict.ResolveTextureCoordinateUnits(
            std::array<std::uint32_t, 1>{0x84DFU})),
        "GLES1 single-stage texture coordinate array fallback is disabled",
        std::runtime_error);
}

TEST_CASE("BND34 GLES1 matrix palette performs weighted transforms and restores contexts") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1, 0, 0}) == 1U);
    const auto surface = AuditSurface(f), context = AuditContext(f, 1U);
    AuditBind(f, context, surface);
    const auto gl = [&](std::string_view name, std::initializer_list<std::uint32_t> args = {}) {
        const auto entry = f.boundary.Symbols().Lookup("libGLESv1_CM.so", name);
        REQUIRE(entry.has_value());
        return BoundaryCallAddress(f, entry->Value(), {args.begin(), args.size()});
    };
    const auto word = [](float value) { return std::bit_cast<std::uint32_t>(value); };
    const auto vertices = f.output.Add(256), weights = f.output.Add(384), indices = f.output.Add(448);
    const std::array positions{-5.0F, -1.0F, -1.0F, -1.0F, -5.0F, 3.0F};
    for (std::size_t i = 0; i < positions.size(); ++i) f.bus.Write32(vertices.Add(i * 4), word(positions[i]), 1U);
    for (std::size_t i = 0; i < 6; ++i) f.bus.Write32(weights.Add(i * 4), word(0.5F), 1U);
    f.bus.Write32(indices, 0x01000100U, 1U); f.bus.Write32(indices.Add(4), 0x00000100U, 1U);
    gl("glVertexPointer", {2, 0x1406, 0, vertices.Value()});
    gl("glWeightPointerOES", {2, 0x1406, 0, weights.Value()});
    gl("glMatrixIndexPointerOES", {2, 0x1401, 0, indices.Value()});
    gl("glEnableClientState", {0x8074}); gl("glEnableClientState", {0x86AD}); gl("glEnableClientState", {0x8844});
    gl("glColor4f", {word(1), 0, 0, word(1)});
    gl("glMatrixMode", {0x8840}); gl("glTranslatef", {word(2), 0, 0});
    gl("glCurrentPaletteMatrixOES", {1});
    gl("glMatrixMode", {0x1700}); gl("glTranslatef", {word(6), 0, 0});
    gl("glLoadPaletteFromModelViewMatrixOES"); gl("glLoadIdentity");
    const auto draw = [&] {
        gl("glClearColor", {0, 0, 0, word(1)}); gl("glClear", {0x4000});
        gl("glDrawArrays", {4, 0, 3});
        gl("glReadPixels", {0, 0, 1, 1, 0x1908, 0x1401, f.output.Add(512).Value()});
        REQUIRE(gl("glGetError") == 0U);
        return f.bus.Read32(f.output.Add(512), 1U);
    };
    CHECK(draw() == 0xFF000000U);
    gl("glEnable", {0x8840}); CHECK(draw() == 0xFF0000FFU);
    const auto other = AuditContext(f, 1U);
    AuditBind(f, other, surface); gl("glCurrentPaletteMatrixOES", {7});
    AuditBind(f, context, surface);
    gl("glGetIntegerv", {0x8843, f.output.Add(520).Value()}); CHECK(f.bus.Read32(f.output.Add(520), 1U) == 1U);
    CHECK(draw() == 0xFF0000FFU);
    gl("glDisable", {0x8840}); CHECK(draw() == 0xFF000000U);
    gl("glPixelStorei", {0x0D05, 8});
    for (std::uint32_t i = 0; i < 16; i += 4) f.bus.Write32(f.output.Add(600 + i), 0xAABBCCDDU, 1U);
    gl("glReadPixels", {0, 0, 1, 2, 0x1908, 0x1401, f.output.Add(600).Value()});
    CHECK(f.bus.Read32(f.output.Add(604), 1U) == 0xAABBCCDDU);
    CHECK(f.bus.Read32(f.output.Add(608), 1U) == 0xFF000000U);
    CHECK(f.bus.Read32(f.output.Add(612), 1U) == 0xAABBCCDDU);
    CHECK(gl("glGetError") == 0U);
}

TEST_CASE("BND34 GLES1 isolates light fog and client arrays between contexts") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto a = AuditContext(f, 1), b = AuditContext(f, 1), surface = AuditSurface(f);
    AuditBind(f, a, surface);
    f.Call("libGLESv1_CM.so", "glFogf", {0x0B62, 0x40000000});
    f.Call("libGLESv1_CM.so", "glLightf", {0x4000, 0x1205, 0x41000000});
    f.Call("libGLESv1_CM.so", "glEnableClientState", {0x8074});
    AuditBind(f, b, surface);
    f.Call("libGLESv1_CM.so", "glGetFloatv", {0x0B62, f.output.Value()});
    CHECK(f.bus.Read32(f.output, 1) == 0x3F800000U);
    CHECK(f.Call("libGLESv1_CM.so", "glIsEnabled", {0x8074}) == 0U);
    AuditBind(f, a, surface);
    f.Call("libGLESv1_CM.so", "glGetFloatv", {0x0B62, f.output.Value()});
    CHECK(f.bus.Read32(f.output, 1) == 0x40000000U);
    f.Call("libGLESv1_CM.so", "glGetLightfv", {0x4000, 0x1205, f.output.Value()});
    CHECK(f.bus.Read32(f.output, 1) == 0x41000000U);
    CHECK(f.Call("libGLESv1_CM.so", "glIsEnabled", {0x8074}) == 1U);
}
