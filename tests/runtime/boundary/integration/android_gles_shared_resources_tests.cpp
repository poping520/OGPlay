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

TEST_CASE("Android boundary GPU trace ring retains the newest raw calls") {
    BoundaryFixture fixture;
    constexpr std::size_t calls = 2050U;
    for (std::size_t index = 0; index < calls; ++index) {
        CHECK(fixture.Call("libEGL.so", "eglGetDisplay") == 1U);
    }
    const auto trace = fixture.boundary.Trace("eglGetDisplay", calls);
    REQUIRE(trace.size() == 2048U);
    for (const auto& entry : trace) {
        CHECK(entry.call == "eglGetDisplay");
        CHECK(entry.arguments.at("r0") == "0");
    }
}

TEST_CASE("Android boundary shares GLES object namespace and texture state") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);

    const auto names = fixture.output;
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGenTextures",
                                   {1U, names.Value()}));
    const auto from_gles1 = fixture.bus.Read32(names, 1);
    REQUIRE(from_gles1 != 0U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glActiveTexture",
                                   {0x84C1U}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindTexture",
                                   {0x0DE1U, from_gles1}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                                   {0x8069U, names.Add(8U).Value()}));
    CHECK(fixture.bus.Read32(names.Add(8U), 1) == from_gles1);

    static_cast<void>(fixture.Call("libGLESv2.so", "glGenTextures",
                                   {1U, names.Add(4U).Value()}));
    const auto from_gles2 = fixture.bus.Read32(names.Add(4U), 1);
    REQUIRE(from_gles2 != 0U);
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glBindTexture",
                                   {0x0DE1U, from_gles2}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glGetIntegerv",
                                   {0x8069U, names.Add(8U).Value()}));
    CHECK(fixture.bus.Read32(names.Add(8U), 1) == from_gles2);

    fixture.bus.Write32(names.Add(12U), from_gles2, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteTextures",
                                   {1U, names.Add(12U).Value()}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                                   {0x8069U, names.Add(8U).Value()}));
    CHECK(fixture.bus.Read32(names.Add(8U), 1) == 0U);

    fixture.bus.Write32(names.Add(12U), from_gles1, 1);
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glDeleteTextures",
                                   {1U, names.Add(12U).Value()}));
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
}

TEST_CASE("DVM-83 managed Java GLES calls share native texture state") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1U);
    const auto names = fixture.output;
    static_cast<void>(fixture.Call("libGLESv2.so", "glGenTextures",
                                   {1U, names.Value()}));
    const auto texture = fixture.bus.Read32(names, 1U);
    REQUIRE(texture != 0U);
    const std::array bind{0x0DE1U, texture};
    CHECK(fixture.boundary.InvokeManagedGles(
              ogplay::gles::GlesApi::gles2, "glBindTexture", bind, 1U) == 0U);
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                                   {0x8069U, names.Add(4U).Value()}));
    CHECK(fixture.bus.Read32(names.Add(4U), 1U) == texture);
    CHECK_THROWS_AS(([&] {
        static_cast<void>(fixture.boundary.InvokeManagedGles(
            ogplay::gles::GlesApi::gles2, "glNoSuchEntry", {}, 1U));
    }()), std::invalid_argument);
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
}

TEST_CASE("GLES2 active texture selects the GLES1 texture matrix unit") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1U);
    const auto output = fixture.output;

    CHECK(fixture.Call("libGLESv2.so", "glActiveTexture", {0x84C1U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glMatrixMode",
                       {ogplay::runtime::detail::kGles1Texture}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glLoadIdentity") == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glTranslatef",
              {std::bit_cast<std::uint32_t>(1.0F),
               std::bit_cast<std::uint32_t>(2.0F),
               std::bit_cast<std::uint32_t>(3.0F)}) == 0U);

    CHECK(fixture.Call("libGLESv1_CM.so", "glActiveTexture", {0x84C0U}) ==
          0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetFloatv",
                       {0x0BA8U, output.Value()}) == 0U);
    CHECK(std::bit_cast<float>(fixture.bus.Read32(output.Add(48U), 1U)) ==
          doctest::Approx(0.0F));

    CHECK(fixture.Call("libGLESv1_CM.so", "glActiveTexture", {0x84C1U}) ==
          0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetFloatv",
                       {0x0BA8U, output.Value()}) == 0U);
    CHECK(std::bit_cast<float>(fixture.bus.Read32(output.Add(48U), 1U)) ==
          doctest::Approx(1.0F));
    CHECK(std::bit_cast<float>(fixture.bus.Read32(output.Add(52U), 1U)) ==
          doctest::Approx(2.0F));
    CHECK(std::bit_cast<float>(fixture.bus.Read32(output.Add(56U), 1U)) ==
          doctest::Approx(3.0F));
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
}

TEST_CASE("shared texture bindings preserve independent GLES2 targets") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1U);
    const auto output = fixture.output;
    CHECK(fixture.Call("libGLESv2.so", "glGenTextures",
                       {3U, output.Value()}) == 0U);
    const auto texture_2d = fixture.bus.Read32(output, 1U);
    const auto texture_cube = fixture.bus.Read32(output.Add(4U), 1U);
    const auto texture_gles1 = fixture.bus.Read32(output.Add(8U), 1U);
    REQUIRE(texture_2d != 0U);
    REQUIRE(texture_cube != 0U);
    REQUIRE(texture_gles1 != 0U);

    CHECK(fixture.Call("libGLESv2.so", "glActiveTexture", {0x84C0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glBindTexture",
                       {0x0DE1U, texture_2d}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glBindTexture",
                       {0x8513U, texture_cube}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8069U, output.Add(32U).Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8514U, output.Add(36U).Value()}) == 0U);
    CHECK(fixture.bus.Read32(output.Add(32U), 1U) == texture_2d);
    CHECK(fixture.bus.Read32(output.Add(36U), 1U) == texture_cube);

    CHECK(fixture.Call("libGLESv1_CM.so", "glBindTexture",
                       {0x0DE1U, texture_gles1}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8069U, output.Add(32U).Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8514U, output.Add(36U).Value()}) == 0U);
    CHECK(fixture.bus.Read32(output.Add(32U), 1U) == texture_gles1);
    CHECK(fixture.bus.Read32(output.Add(36U), 1U) == texture_cube);

    fixture.bus.Write32(output.Add(16U), texture_cube, 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDeleteTextures",
                       {1U, output.Add(16U).Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8069U, output.Add(32U).Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8514U, output.Add(36U).Value()}) == 0U);
    CHECK(fixture.bus.Read32(output.Add(32U), 1U) == texture_gles1);
    CHECK(fixture.bus.Read32(output.Add(36U), 1U) == 0U);

    ogplay::runtime::SharedGlState metadata;
    metadata.BindTexture(0x0DE1U, 41U);
    metadata.SetTextureBaseFormat(0x0DE1U, 0x1908U);
    metadata.SetGenerateMipmap(0x0DE1U, true);
    metadata.BindTexture(0x8513U, 42U);
    metadata.SetTextureBaseFormat(0x8513U, 0x1907U);
    metadata.SetGenerateMipmap(0x8513U, true);
    metadata.SetActiveTexture(0x84C1U);
    metadata.BindTexture(0x0DE1U, 42U);
    metadata.BindTexture(0x8513U, 42U);
    metadata.SetActiveTexture(0x84C0U);
    CHECK(metadata.TextureBaseFormat(0x0DE1U) == 0x1908U);
    CHECK(metadata.TextureBaseFormat(0x8513U) == 0x1907U);
    CHECK(metadata.GenerateMipmapEnabled(0x0DE1U));
    CHECK(metadata.GenerateMipmapEnabled(0x8513U));
    const std::array deleted_cube{42U};
    metadata.DeleteTextures(deleted_cube);
    CHECK(metadata.BoundTexture(0x0DE1U) == 41U);
    CHECK(metadata.BoundTexture(0x8513U) == 0U);
    CHECK(metadata.BoundTexture(0x84C1U, 0x0DE1U) == 0U);
    CHECK(metadata.BoundTexture(0x84C1U, 0x8513U) == 0U);
    CHECK(metadata.TextureBaseFormat(0x0DE1U) == 0x1908U);
    CHECK_FALSE(metadata.TextureBaseFormat(0x8513U).has_value());
    CHECK(metadata.GenerateMipmapEnabled(0x0DE1U));
    CHECK_FALSE(metadata.GenerateMipmapEnabled(0x8513U));
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
}

TEST_CASE("BND-29 GLES2 invalid capabilities latch errors without changing state") {
    REQUIRE(ogplay::gles::IsNativeAngleEglAvailable());
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);
    CHECK(fixture.Call("libGLESv2.so", "glEnable", {0x0BE2U}) == 0);
    for (const auto name : {"glEnable", "glDisable", "glIsEnabled"}) {
        for (const auto cap : {0x0DE1U, 0xffffffffU}) {
            CHECK(fixture.Call("libGLESv2.so", name, {cap}) == 0);
            CHECK(fixture.Call("libGLESv2.so", "glIsEnabled", {0x0BE2U}) == 1);
            CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0x0500U);
            CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0);
        }
    }
    CHECK(fixture.Call("libGLESv2.so", "glEnable", {0x0DE1U}) == 0);
    CHECK(fixture.Call("libGLESv2.so", "glDisable", {0xffffffffU}) == 0);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0500U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0);
    CHECK(fixture.Call("libGLESv2.so", "glDisable", {0x0BE2U}) == 0);
    CHECK(fixture.Call("libGLESv2.so", "glIsEnabled", {0x0BE2U}) == 0);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0DE1U}) == 0);
    CHECK(fixture.Call("libGLESv1_CM.so", "glIsEnabled", {0x0DE1U}) == 1);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0);
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1);
    CHECK_THROWS_AS(fixture.Call("libGLESv2.so", "glEnable", {0x0DE1U}), std::runtime_error);
}

TEST_CASE("GLES draw negative ranges latch INVALID_VALUE across API families") {
    REQUIRE(ogplay::gles::IsNativeAngleEglAvailable());
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);

    for (const auto library : {"libGLESv1_CM.so", "libGLESv2.so"}) {
        CAPTURE(library);
        CHECK(fixture.Call(library, "glDrawArrays",
                           {0x0004U, 0U, 0xFFFFFFFFU}) == 0U);
        CHECK(fixture.Call(library, "glGetError") == 0x0501U);
        CHECK(fixture.Call(library, "glGetError") == 0U);
        CHECK(fixture.Call(library, "glDrawElements",
                           {0x0004U, 0xFFFFFFFFU, 0x1403U, 0U}) == 0U);
        CHECK(fixture.Call(library, "glGetError") == 0x0501U);
        CHECK(fixture.Call(library, "glGetError") == 0U);
    }
}

TEST_CASE("Android boundary shares framebuffer raster and capability state") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);
    const auto output = fixture.output;

    static_cast<void>(fixture.Call("libGLESv2.so", "glGenFramebuffers",
                                   {1U, output.Value()}));
    const auto framebuffer = fixture.bus.Read32(output, 1);
    REQUIRE(framebuffer != 0U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindFramebuffer",
                                   {0x8D40U, framebuffer}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                                   {0x8CA6U, output.Add(16U).Value()}));
    CHECK(fixture.bus.Read32(output.Add(16U), 1) == framebuffer);

    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glViewport",
                                   {1U, 2U, 3U, 1U}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glGetIntegerv",
                                   {0x0BA2U, output.Add(32U).Value()}));
    CHECK(fixture.bus.Read32(output.Add(32U), 1) == 1U);
    CHECK(fixture.bus.Read32(output.Add(36U), 1) == 2U);
    CHECK(fixture.bus.Read32(output.Add(40U), 1) == 3U);
    CHECK(fixture.bus.Read32(output.Add(44U), 1) == 1U);

    static_cast<void>(fixture.Call("libGLESv2.so", "glScissor",
                                   {0U, 1U, 2U, 2U}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                                   {0x0C10U, output.Add(48U).Value()}));
    CHECK(fixture.bus.Read32(output.Add(52U), 1) == 1U);
    CHECK(fixture.bus.Read32(output.Add(56U), 1) == 2U);
    CHECK(fixture.bus.Read32(output.Add(60U), 1) == 2U);

    static_cast<void>(fixture.Call("libGLESv2.so", "glEnable", {0x0BE2U}));
    CHECK(fixture.Call("libGLESv1_CM.so", "glIsEnabled", {0x0BE2U}) == 1U);
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glDisable", {0x0BE2U}));
    CHECK(fixture.Call("libGLESv2.so", "glIsEnabled", {0x0BE2U}) == 0U);
}

TEST_CASE("supersample cross API queries preserve logical raster state") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture(2U);
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1U);
    const auto output = fixture.output;

    CHECK(fixture.Call("libGLESv1_CM.so", "glViewport",
                       {1U, 2U, 100U, 200U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0BA2U, output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(output, 1U) == 1U);
    CHECK(fixture.bus.Read32(output.Add(4U), 1U) == 2U);
    CHECK(fixture.bus.Read32(output.Add(8U), 1U) == 100U);
    CHECK(fixture.bus.Read32(output.Add(12U), 1U) == 200U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                       {0x0BA2U, output.Add(32U).Value()}) == 0U);
    CHECK(fixture.bus.Read32(output.Add(32U), 1U) == 1U);
    CHECK(fixture.bus.Read32(output.Add(36U), 1U) == 2U);
    CHECK(fixture.bus.Read32(output.Add(40U), 1U) == 100U);
    CHECK(fixture.bus.Read32(output.Add(44U), 1U) == 200U);

    CHECK(fixture.Call("libGLESv2.so", "glScissor",
                       {3U, 4U, 50U, 60U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                       {0x0C10U, output.Add(16U).Value()}) == 0U);
    CHECK(fixture.bus.Read32(output.Add(16U), 1U) == 3U);
    CHECK(fixture.bus.Read32(output.Add(20U), 1U) == 4U);
    CHECK(fixture.bus.Read32(output.Add(24U), 1U) == 50U);
    CHECK(fixture.bus.Read32(output.Add(28U), 1U) == 60U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0C10U, output.Add(48U).Value()}) == 0U);
    CHECK(fixture.bus.Read32(output.Add(48U), 1U) == 3U);
    CHECK(fixture.bus.Read32(output.Add(52U), 1U) == 4U);
    CHECK(fixture.bus.Read32(output.Add(56U), 1U) == 50U);
    CHECK(fixture.bus.Read32(output.Add(60U), 1U) == 60U);
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
}

TEST_CASE("new GL context exposes logical default raster state") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture(2U);
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1U);
    const auto output = fixture.output;

    const auto check_box = [&fixture](const ogplay::memory::GuestAddress address) {
        CHECK(fixture.bus.Read32(address, 1U) == 0U);
        CHECK(fixture.bus.Read32(address.Add(4U), 1U) == 0U);
        CHECK(fixture.bus.Read32(address.Add(8U), 1U) == 4U);
        CHECK(fixture.bus.Read32(address.Add(12U), 1U) == 3U);
    };

    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0BA2U, output.Value()}) == 0U);
    check_box(output);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                       {0x0BA2U, output.Add(16U).Value()}) == 0U);
    check_box(output.Add(16U));

    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0C10U, output.Add(32U).Value()}) == 0U);
    check_box(output.Add(32U));
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                       {0x0C10U, output.Add(48U).Value()}) == 0U);
    check_box(output.Add(48U));

    constexpr auto kDither = UINT32_C(0x0BD0);
    CHECK(fixture.Call("libGLESv2.so", "glIsEnabled", {kDither}) == 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glIsEnabled", {kDither}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
}

TEST_CASE("GLES1 OES mapbuffer uses a guest arena and never leaks host pointers") {
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    const auto name_output = fixture.output.Add(0x500U);
    const auto data = fixture.output.Add(0x520U);
    const auto pointer_output = fixture.output.Add(0x540U);
    constexpr std::uint32_t kArrayBuffer = 0x8892U;
    constexpr std::uint32_t kStaticDraw = 0x88E4U;
    constexpr std::uint32_t kWriteOnlyOes = 0x88B9U;
    constexpr std::uint32_t kBufferMapPointerOes = 0x88BDU;
    constexpr std::array<std::uint32_t, 4> initial{
        0x11111111U, 0x22222222U, 0x33333333U, 0x44444444U};
    for (std::size_t index = 0; index < initial.size(); ++index) {
        fixture.bus.Write32(data.Add(index * sizeof(std::uint32_t)),
                            initial[index], 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glGenBuffers",
                       {1U, name_output.Value()}) == 0U);
    const auto buffer = fixture.bus.Read32(name_output, 1U);
    REQUIRE(buffer != 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glBindBuffer",
                       {kArrayBuffer, buffer}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glBufferData",
                       {kArrayBuffer, sizeof(initial), data.Value(),
                        kStaticDraw}) == 0U);

    fixture.bus.Write32(pointer_output, 0xffffffffU, 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetBufferPointervOES",
                       {kArrayBuffer, kBufferMapPointerOes,
                        pointer_output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(pointer_output, 1U) == 0U);

    const auto mapped = fixture.Call("libGLESv1_CM.so", "glMapBufferOES",
                                     {kArrayBuffer, kWriteOnlyOes});
    REQUIRE(mapped >= ogplay::runtime::detail::kGles1MapBufferArenaBegin);
    CHECK(mapped < ogplay::runtime::detail::kGles1MapBufferArenaBegin +
                       ogplay::runtime::detail::kGles1MapBufferArenaBytes);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetBufferPointervOES",
                       {kArrayBuffer, kBufferMapPointerOes,
                        pointer_output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(pointer_output, 1U) == mapped);
    fixture.bus.Write32(ogplay::memory::GuestAddress{mapped}, 0xa5a5a5a5U, 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glUnmapBufferOES",
                       {kArrayBuffer}) == 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetBufferPointervOES",
                       {kArrayBuffer, kBufferMapPointerOes,
                        pointer_output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(pointer_output, 1U) == 0U);

    CHECK(fixture.Call("libGLESv1_CM.so", "glMapBufferOES",
                       {kArrayBuffer, 0x88B8U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0500U);
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("Android GLES boundary transfers buffer and texture resources") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);
    const auto names = fixture.output;
    const auto buffer_data = fixture.output.Add(0x100);
    const auto texture_data = fixture.output.Add(0x200);
    constexpr std::array<std::byte, 12> vertices{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8},
        std::byte{9}, std::byte{10}, std::byte{11}, std::byte{12}};
    constexpr std::array<std::byte, 16> pixels{
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
        std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255},
        std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    fixture.memory.Write(buffer_data, vertices, 1);
    fixture.memory.Write(texture_data, pixels, 1);

    static_cast<void>(fixture.Call("libGLESv2.so", "glGenFramebuffers",
                                   {1, names.Value()}));
    const auto framebuffer = fixture.bus.Read32(names, 1);
    REQUIRE(framebuffer != 0U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glGenRenderbuffers",
                                   {1, names.Add(4).Value()}));
    const auto renderbuffer = fixture.bus.Read32(names.Add(4), 1);
    REQUIRE(renderbuffer != 0U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindFramebuffer",
                                   {0x8D40U, framebuffer}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindRenderbuffer",
                                   {0x8D41U, renderbuffer}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glRenderbufferStorage",
                                   {0x8D41U, 0x8056U, 2U, 2U}));
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glFramebufferRenderbuffer",
        {0x8D40U, 0x8CE0U, 0x8D41U, renderbuffer}));
    CHECK(fixture.Call("libGLESv2.so", "glCheckFramebufferStatus",
                       {0x8D40U}) == 0x8CD5U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glRenderbufferStorage",
                                   {0x8D41U, 0x8058U, 2U, 2U}));
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glGetRenderbufferParameteriv",
        {0x8D41U, 0x8D44U, names.Add(8).Value()}));
    CHECK(fixture.bus.Read32(names.Add(8), 1) == 0x8058U);
    CHECK(fixture.Call("libGLESv2.so", "glCheckFramebufferStatus",
                       {0x8D40U}) == 0x8CD5U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindFramebuffer",
                                   {0x8D40U, 0U}));

    static_cast<void>(fixture.Call("libGLESv2.so", "glGenBuffers",
                                   {2, names.Value()}));
    const auto vertex_buffer = fixture.bus.Read32(names, 1);
    const auto index_buffer = fixture.bus.Read32(names.Add(4), 1);
    CHECK(vertex_buffer != 0); CHECK(index_buffer != 0);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindBuffer",
                                   {0x8892U, vertex_buffer}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBufferData",
                                   {0x8892U, 12, buffer_data.Value(), 0x88e4U}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBufferData",
                                   {0x8892U, 16, 0, 0x88e8U}));
    CHECK_THROWS_AS(
        fixture.Call("libGLESv2.so", "glBufferData",
                     {0x8892U, 12, 0x1000U, 0x88e4U}),
        ogplay::memory::MemoryFault);
    static_cast<void>(fixture.Call("libGLESv2.so", "glEnableVertexAttribArray", {0}));
    fixture.bus.Write32(fixture.stack, 0, 1);
    fixture.bus.Write32(fixture.stack.Add(4), 0, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glVertexAttribPointer",
                                   {0, 3, 0x1406U, 0}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glDisableVertexAttribArray", {0}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindBuffer", {0x8892U, 0}));
    fixture.bus.Write32(fixture.stack, 0, 1);
    fixture.bus.Write32(fixture.stack.Add(4), buffer_data.Value(), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glVertexAttribPointer",
                                   {0, 3, 0x1406U, 0}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindBuffer",
                                   {0x8892U, vertex_buffer}));

    static_cast<void>(fixture.Call("libGLESv2.so", "glGenTextures",
                                   {1, names.Value()}));
    const auto texture = fixture.bus.Read32(names, 1);
    REQUIRE(texture != 0);
    static_cast<void>(fixture.Call("libGLESv2.so", "glActiveTexture", {0x84c0U}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindTexture",
                                   {0x0de1U, texture}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glPixelStorei", {0x0cf5U, 1}));
    CHECK_THROWS_AS(fixture.Call("libGLESv2.so", "glPixelStorei", {0x0cf5U, 3}),
                    ogplay::gles::GlesTransferStateError);
    static_cast<void>(fixture.Call("libGLESv2.so", "glTexParameteri",
                                   {0x0de1U, 0x2801U, 0x2600U}));
    CHECK(fixture.Call("libGLESv2.so", "glTexParameteri",
                       {0x0de1U, 0x8191U, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0x0500U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);
    fixture.bus.Write32(fixture.stack, 2, 1);
    fixture.bus.Write32(fixture.stack.Add(4), 0, 1);
    fixture.bus.Write32(fixture.stack.Add(8), 0x1908U, 1);
    fixture.bus.Write32(fixture.stack.Add(12), 0x1401U, 1);
    fixture.bus.Write32(fixture.stack.Add(16), texture_data.Value(), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glTexImage2D",
                                   {0x0de1U, 0, 0x1908U, 2}));
    fixture.bus.Write32(fixture.stack, 1, 1);
    fixture.bus.Write32(fixture.stack.Add(4), 1, 1);
    fixture.bus.Write32(fixture.stack.Add(8), 0x1908U, 1);
    fixture.bus.Write32(fixture.stack.Add(12), 0x1401U, 1);
    fixture.bus.Write32(fixture.stack.Add(16), texture_data.Value(), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glTexSubImage2D",
                                   {0x0de1U, 0, 0, 0}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glGenerateMipmap",
                                   {0x0de1U}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindFramebuffer",
                                   {0x8D40U, framebuffer}));
    fixture.bus.Write32(fixture.stack, 0U, 1U);
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glFramebufferTexture2D",
        {0x8D40U, 0x8CE0U, 0x0DE1U, texture}));
    CHECK(fixture.Call("libGLESv2.so", "glCheckFramebufferStatus",
                       {0x8D40U}) == 0x8CD5U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindFramebuffer",
                                   {0x8D40U, 0U}));
    CHECK_THROWS_AS(fixture.Call("libGLESv2.so", "glGenBuffers", {1, 0}),
                    ogplay::gles::GuestTransferError);

    fixture.bus.Write32(names, texture, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteTextures",
                                   {1, names.Value()}));
    fixture.bus.Write32(names, vertex_buffer, 1);
    fixture.bus.Write32(names.Add(4), index_buffer, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteBuffers",
                                   {2, names.Value()}));
    fixture.bus.Write32(names, framebuffer, 1);
    fixture.bus.Write32(names.Add(4), renderbuffer, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteFramebuffers",
                                   {1, names.Value()}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteRenderbuffers",
                                   {1, names.Add(4).Value()}));
}

TEST_CASE("BND34 ES3 mapping flush ranges arena reuse and VAO deletion remain coherent") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1, 0, 0}) == 1U);
    const auto surface = AuditSurface(f), context = AuditContext(f);
    AuditBind(f, context, surface);
    const auto data = f.output.Add(640);
    f.bus.Write32(data, 0x11223344U, 1U); f.bus.Write32(data.Add(4), 0x55667788U, 1U);
    const auto buffer = AuditName(f, "glGenBuffers");
    AuditGl(f, "glBindBuffer", {0x8892, buffer}); AuditGl(f, "glBufferData", {0x8892, 8, data.Value(), 0x88E8});
    const auto mapped = AuditGl(f, "glMapBufferRange", {0x8892, 0, 8, 0x12}); REQUIRE(mapped != 0U);
    f.bus.Write32(ogplay::memory::GuestAddress{mapped}, 0xABCDEF12U, 1U);
    AuditGl(f, "glFlushMappedBufferRange", {0x8892, 0, 4});
    f.bus.Write32(ogplay::memory::GuestAddress{mapped}, 0x98765432U, 1U);
    REQUIRE(AuditGl(f, "glUnmapBuffer", {0x8892}) == 1U);
    const auto preserved = AuditName(f, "glGenBuffers");
    AuditGl(f, "glBindBuffer", {0x8892, preserved});
    AuditGl(f, "glBufferData", {0x8892, 8, data.Value(), 0x88E8});
    const auto write_only = AuditGl(
        f, "glMapBufferRange", {0x8892, 0, 8, 0x2});
    REQUIRE(write_only != 0U);
    f.bus.Write32(ogplay::memory::GuestAddress{write_only}, 0xDEADBEEFU, 1U);
    REQUIRE(AuditGl(f, "glUnmapBuffer", {0x8892}) == 1U);
    const auto verify_preserved = AuditGl(
        f, "glMapBufferRange", {0x8892, 0, 8, 0x1});
    REQUIRE(verify_preserved != 0U);
    CHECK(f.bus.Read32(ogplay::memory::GuestAddress{verify_preserved}, 1U) ==
          0xDEADBEEFU);
    CHECK(f.bus.Read32(ogplay::memory::GuestAddress{verify_preserved}.Add(4U),
                       1U) == 0x55667788U);
    REQUIRE(AuditGl(f, "glUnmapBuffer", {0x8892}) == 1U);
    AuditGl(f, "glBindBuffer", {0x8892, buffer});
    const auto read = AuditGl(f, "glMapBufferRange", {0x8892, 0, 8, 1}); REQUIRE(read != 0U);
    CHECK(f.bus.Read32(ogplay::memory::GuestAddress{read}, 1U) == 0xABCDEF12U);
    // Keep this mapping alive while repeatedly mapping a second buffer: the arena must reuse gaps.
    const auto second = AuditName(f, "glGenBuffers");
    AuditGl(f, "glBindBuffer", {0x8892, second}); AuditGl(f, "glBufferData", {0x8892, 2U * 1024U * 1024U, 0, 0x88E8});
    for (unsigned i = 0; i < 10; ++i) {
        REQUIRE(AuditGl(f, "glMapBufferRange", {0x8892, 0, 2U * 1024U * 1024U, 2}) != 0U);
        REQUIRE(AuditGl(f, "glUnmapBuffer", {0x8892}) == 1U);
    }
    AuditGl(f, "glBindBuffer", {0x8892, buffer}); REQUIRE(AuditGl(f, "glUnmapBuffer", {0x8892}) == 1U);
    const auto vao = AuditName(f, "glGenVertexArrays"); AuditGl(f, "glBindVertexArray", {vao});
    AuditGl(f, "glBindBuffer", {0x8893, buffer}); AuditGl(f, "glEnableVertexAttribArray", {0});
    AuditGl(f, "glVertexAttribI4ui", {3, 0xFFFFFFFFU, 0xABCDEF12U, 7, 9});
    AuditGl(f, "glVertexAttribDivisor", {3, 2});
    f.bus.Write32(data, 0U, 1U); f.bus.Write32(data.Add(4), 0U, 1U);
    f.Call("libGLESv1_CM.so", "glBindBuffer", {0x8892, 0});
    f.Call("libGLESv1_CM.so", "glVertexPointer", {2, 0x1406, 0, data.Value()});
    f.Call("libGLESv1_CM.so", "glEnableClientState", {0x8074});
    f.Call("libGLESv1_CM.so", "glDrawArrays", {0, 0, 1});
    CHECK(AuditInteger(f, 0x85B5) == vao);
    AuditGl(f, "glGetVertexAttribIuiv", {3, 0x8626, data.Value()});
    CHECK(f.bus.Read32(data, 1U) == 0xFFFFFFFFU); CHECK(f.bus.Read32(data.Add(4), 1U) == 0xABCDEF12U);
    AuditGl(f, "glGetVertexAttribiv", {3, 0x88FE, data.Value()}); CHECK(f.bus.Read32(data, 1U) == 2U);
    f.bus.Write32(data, vao, 1U); AuditGl(f, "glDeleteVertexArrays", {1, data.Value()});
    CHECK(AuditInteger(f, 0x85B5) == 0U); CHECK(AuditInteger(f, 0x8895) == 0U);
    AuditGl(f, "glGetVertexAttribiv", {0, 0x8622, data.Value()}); CHECK(f.bus.Read32(data, 1U) == 0U);
    AuditGl(f, "glViewport", {1, 2, 3, 4});
    f.bus.Write32(data.Add(32), 0xDEADBEEFU, 1U);
    AuditGl(f, "glGetInteger64v", {0x0BA2, data.Value()});
    for (std::uint32_t i = 0; i < 4; ++i) {
        CHECK(f.bus.Read32(data.Add(i * 8), 1U) == i + 1U);
        CHECK(f.bus.Read32(data.Add(i * 8 + 4), 1U) == 0U);
    }
    CHECK(f.bus.Read32(data.Add(32), 1U) == 0xDEADBEEFU);
    CHECK(AuditGl(f, "glGetError") == 0U);
}

TEST_CASE("BND34 GLES1 mappings survive releasing a different context") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1, 0, 0}) == 1U);
    const auto surface = AuditSurface(f), a = AuditContext(f, 1U), b = AuditContext(f, 1U);
    const auto map = [&] {
        f.Call("libGLESv1_CM.so", "glGenBuffers", {1, f.output.Add(256).Value()});
        const auto buffer = f.bus.Read32(f.output.Add(256), 1U);
        f.Call("libGLESv1_CM.so", "glBindBuffer", {0x8892, buffer});
        f.Call("libGLESv1_CM.so", "glBufferData", {0x8892, 16, 0, 0x88E8});
        const auto pointer = f.Call("libGLESv1_CM.so", "glMapBufferOES", {0x8892, 0x88B9});
        REQUIRE(pointer != 0U); return std::pair{buffer, pointer};
    };
    AuditBind(f, a, surface); const auto first = map();
    AuditBind(f, b, surface); const auto second = map();
    REQUIRE(first.first == second.first); CHECK(first.second != second.second);
    f.bus.Write32(ogplay::memory::GuestAddress{first.second}, 0x12345678U, 1U);
    f.bus.Write32(ogplay::memory::GuestAddress{second.second}, 0x90ABCDEFU, 1U);
    REQUIRE(f.Call("libEGL.so", "eglReleaseThread") == 1U);
    AuditBind(f, a, surface);
    f.Call("libGLESv1_CM.so", "glGetBufferPointervOES", {0x8892, 0x88BD, f.output.Add(256).Value()});
    CHECK(f.bus.Read32(f.output.Add(256), 1U) == first.second);
    REQUIRE(f.Call("libGLESv1_CM.so", "glUnmapBufferOES", {0x8892}) == 1U);
    AuditBind(f, b, surface);
    REQUIRE(f.Call("libGLESv1_CM.so", "glUnmapBufferOES", {0x8892}) == 1U);
    CHECK(f.Call("libGLESv1_CM.so", "glGetError") == 0U);
}

TEST_CASE("DVM-238 OES mapping shares guest identity and preserves bytes across GLES APIs") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    for (const auto version : {2U, 3U}) {
        CAPTURE(version);
        BoundaryFixture f;
        REQUIRE(f.Call("libEGL.so", "eglInitialize", {1, 0, 0}) == 1);
        const auto surface = AuditSurface(f), context = AuditContext(f, version);
        AuditBind(f, context, surface);
        const auto buffer = AuditName(f, "glGenBuffers");
        AuditGl(f, "glBindBuffer", {0x8892, buffer});
        f.bus.Write32(f.output.Add(32), 0x11223344);
        f.bus.Write32(f.output.Add(36), 0x55667788);
        AuditGl(f, "glBufferData", {0x8892, 8, f.output.Add(32).Value(), 0x88E8});
        const auto mapped = AuditGl(f, "glMapBufferOES", {0x8892, 0x88B9});
        REQUIRE(mapped >= ogplay::runtime::kBufferMapArenaBegin);
        CHECK(mapped < ogplay::runtime::kBufferMapArenaBegin + ogplay::runtime::kBufferMapArenaBytes);
        CHECK(f.bus.Read32(ogplay::memory::GuestAddress{mapped}.Add(4)) == 0x55667788);
        f.bus.Write32(ogplay::memory::GuestAddress{mapped}, 0xDEADBEEF);
        f.Call("libGLESv1_CM.so", "glGetBufferPointervOES", {0x8892, 0x88BD, f.output.Add(64).Value()});
        CHECK(f.bus.Read32(f.output.Add(64)) == mapped);
        for (const auto pname : {0x88BBU, 0x911FU}) {
            AuditGl(f, "glGetBufferParameteriv", {0x8892, pname, f.output.Add(64).Value()});
            CHECK(f.bus.Read32(f.output.Add(64)) == (pname == 0x88BBU ? 0x88B9U : 2U));
        }
        CHECK(AuditGl(f, "glMapBufferOES", {0x8892, 0x88B9}) == 0);
        CHECK(AuditGl(f, "glGetError") == 0x0502);
        if (version == 3) {
            AuditGl(f, "glGetBufferPointerv", {0x8892, 0x88BD, f.output.Add(64).Value()});
            CHECK(f.bus.Read32(f.output.Add(64)) == mapped);
            REQUIRE(AuditGl(f, "glUnmapBuffer", {0x8892}) == 1);
        } else REQUIRE(AuditGl(f, "glUnmapBufferOES", {0x8892}) == 1);
        const auto again = AuditGl(f, "glMapBufferOES", {0x8892, 0x88B9});
        REQUIRE(again != 0);
        CHECK(f.bus.Read32(ogplay::memory::GuestAddress{again}) == 0xDEADBEEF);
        CHECK(f.bus.Read32(ogplay::memory::GuestAddress{again}.Add(4)) == 0x55667788);
        REQUIRE(f.Call("libGLESv1_CM.so", "glUnmapBufferOES", {0x8892}) == 1);
        AuditGl(f, "glGetBufferPointervOES", {0x8892, 0x88BD, f.output.Add(64).Value()});
        CHECK(f.bus.Read32(f.output.Add(64)) == 0);
        CHECK(AuditGl(f, "glMapBufferOES", {0x8892, 0x88B8}) == 0);
        CHECK(AuditGl(f, "glGetError") == 0x0500);
        WriteGuestString(f, f.output.Add(96), "glMapBufferOES");
        const auto proc = f.Call("libEGL.so", "eglGetProcAddress", {f.output.Add(96).Value()});
        REQUIRE(proc != 0);
        const auto through_proc = BoundaryCallAddress(f, proc, std::array<std::uint32_t, 2>{0x8892, 0x88B9});
        REQUIRE(through_proc != 0);
        REQUIRE(AuditGl(f, "glUnmapBufferOES", {0x8892}) == 1);
        // Deletion must retire the record before GLuint reuse can occur.
        REQUIRE(AuditGl(f, "glMapBufferOES", {0x8892, 0x88B9}) != 0);
        f.bus.Write32(f.output.Add(64), buffer);
        AuditGl(f, "glDeleteBuffers", {1, f.output.Add(64).Value()});
        AuditGl(f, "glBindBuffer", {0x8892, buffer});
        AuditGl(f, "glBufferData", {0x8892, 8, 0, 0x88E8});
        REQUIRE(AuditGl(f, "glMapBufferOES", {0x8892, 0x88B9}) != 0);
        REQUIRE(AuditGl(f, "glUnmapBufferOES", {0x8892}) == 1);
        CHECK(AuditGl(f, "glGetError") == 0);
    }
}

TEST_CASE("DVM-238 range mappings retain guest flags and explicit flush semantics through OES") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1);
    const auto surface = AuditSurface(f), context = AuditContext(f);
    AuditBind(f, context, surface);
    const auto buffer = AuditName(f, "glGenBuffers");
    AuditGl(f, "glBindBuffer", {0x8892, buffer});
    f.bus.Write32(f.output.Add(32), 0x11223344); f.bus.Write32(f.output.Add(36), 0x55667788);
    AuditGl(f, "glBufferData", {0x8892, 8, f.output.Add(32).Value(), 0x88E8});
    const auto map = AuditGl(f, "glMapBufferRange", {0x8892, 0, 8, 0x12});
    REQUIRE(map != 0);
    AuditGl(f, "glGetBufferPointervOES", {0x8892, 0x88BD, f.output.Add(64).Value()});
    CHECK(f.bus.Read32(f.output.Add(64)) == map);
    AuditGl(f, "glGetBufferParameteriv", {0x8892, 0x911F, f.output.Add(64).Value()});
    CHECK(f.bus.Read32(f.output.Add(64)) == 0x12);
    f.bus.Write32(ogplay::memory::GuestAddress{map}, 0xAABBCCDD);
    AuditGl(f, "glFlushMappedBufferRange", {0x8892, 0, 4});
    f.bus.Write32(ogplay::memory::GuestAddress{map}, 0xFFFFFFFF);
    REQUIRE(AuditGl(f, "glUnmapBufferOES", {0x8892}) == 1);
    const auto read = AuditGl(f, "glMapBufferRange", {0x8892, 0, 8, 1});
    REQUIRE(read != 0);
    CHECK(f.bus.Read32(ogplay::memory::GuestAddress{read}) == 0xAABBCCDD);
    CHECK(f.bus.Read32(ogplay::memory::GuestAddress{read}.Add(4)) == 0x55667788);
    REQUIRE(AuditGl(f, "glUnmapBuffer", {0x8892}) == 1);
    const auto unsync = AuditGl(f, "glMapBufferRange", {0x8892, 0, 8, 0x22});
    REQUIRE(unsync != 0);
    f.bus.Write32(ogplay::memory::GuestAddress{unsync}, 0x12345678);
    REQUIRE(AuditGl(f, "glUnmapBuffer", {0x8892}) == 1);
    const auto verify = AuditGl(f, "glMapBufferRange", {0x8892, 0, 8, 1});
    REQUIRE(verify != 0);
    CHECK(f.bus.Read32(ogplay::memory::GuestAddress{verify}) == 0x12345678);
    CHECK(f.bus.Read32(ogplay::memory::GuestAddress{verify}.Add(4)) == 0x55667788);
    REQUIRE(AuditGl(f, "glUnmapBuffer", {0x8892}) == 1);
    const auto large = AuditName(f, "glGenBuffers");
    AuditGl(f, "glBindBuffer", {0x8892, large});
    AuditGl(f, "glBufferData", {0x8892, ogplay::runtime::kBufferMapArenaBytes + 16, 0, 0x88E8});
    CHECK(AuditGl(f, "glMapBufferOES", {0x8892, 0x88B9}) == 0);
    CHECK(AuditGl(f, "glGetError") == 0x0505);
    AuditGl(f, "glGetBufferParameteriv", {0x8892, 0x88BC, f.output.Add(64).Value()});
    CHECK(f.bus.Read32(f.output.Add(64)) == 0);
}

TEST_CASE("DVM-238 shared contexts keep a mapping until the last owner retires") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    const auto read_string = [&](ogplay::memory::GuestAddress address) {
        std::string value(f.memory.CStringLength(address, 4096, 1), '\0');
        f.memory.Read(address, std::as_writable_bytes(std::span(value)), 1);
        return value;
    };
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1);
    const auto surface = AuditSurface(f), first = AuditContext(f, 3);
    const auto shared = AuditContext(f, 3, first);
    AuditBind(f, first, surface);
    const auto buffer = AuditName(f, "glGenBuffers");
    AuditGl(f, "glBindBuffer", {0x8892, buffer});
    AuditGl(f, "glBufferData", {0x8892, 8, 0, 0x88E8});
    const auto mapped = AuditGl(f, "glMapBufferOES", {0x8892, 0x88B9});
    REQUIRE(mapped != 0);
    f.bus.Write32(ogplay::memory::GuestAddress{mapped}, 0x12345678);
    AuditBind(f, shared, surface);
    AuditGl(f, "glBindBuffer", {0x8892, buffer});
    AuditGl(f, "glGetBufferPointerv", {0x8892, 0x88BD, f.output.Add(64).Value()});
    CHECK(f.bus.Read32(f.output.Add(64)) == mapped);
    REQUIRE(f.Call("libEGL.so", "eglDestroyContext", {1, first}) == 1);
    REQUIRE(AuditGl(f, "glUnmapBufferOES", {0x8892}) == 1);
    const auto verify = AuditGl(f, "glMapBufferRange", {0x8892, 0, 8, 1});
    REQUIRE(verify != 0);
    CHECK(f.bus.Read32(ogplay::memory::GuestAddress{verify}) == 0x12345678);
    REQUIRE(AuditGl(f, "glUnmapBuffer", {0x8892}) == 1);
    const auto extensions = AuditGl(f, "glGetString", {0x1F03});
    CHECK(read_string(ogplay::memory::GuestAddress{extensions}).find("GL_OES_mapbuffer") != std::string::npos);
    const auto count = AuditInteger(f, 0x821D);
    bool found = false;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto name = AuditGl(f, "glGetStringi", {0x1F03, index});
        if (read_string(ogplay::memory::GuestAddress{name}) == "GL_OES_mapbuffer") found = true;
    }
    CHECK(found);
    REQUIRE(AuditGl(f, "glMapBufferOES", {0x8892, 0x88B9}) != 0);
    REQUIRE(f.Call("libEGL.so", "eglMakeCurrent", {1, 0, 0, 0}) == 1);
    REQUIRE(f.Call("libEGL.so", "eglDestroyContext", {1, shared}) == 1);
    const auto fresh = AuditContext(f, 3);
    AuditBind(f, fresh, surface);
    const auto replacement = AuditName(f, "glGenBuffers");
    AuditGl(f, "glBindBuffer", {0x8892, replacement});
    AuditGl(f, "glBufferData", {0x8892, 8, 0, 0x88E8});
    REQUIRE(AuditGl(f, "glMapBufferOES", {0x8892, 0x88B9}) != 0);
    REQUIRE(AuditGl(f, "glUnmapBufferOES", {0x8892}) == 1);
    CHECK(AuditGl(f, "glGetError") == 0);
}

TEST_CASE("BND34 ES3 queries respect scalar and variable output extents") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto context = AuditContext(f), surface = AuditSurface(f);
    AuditBind(f, context, surface);
    const auto sampler = AuditName(f, "glGenSamplers");
    const auto tail = f.output.Add(f.memory.PageSize() - 4);
    AuditGl(f, "glGetSamplerParameteriv", {sampler, 0x2800, tail.Value()});
    CHECK(f.bus.Read32(tail, 1) == 0x2601U);
    AuditGl(f, "glGetSamplerParameterfv", {sampler, 0x813B, tail.Value()});
    CHECK(AuditGl(f, "glGetError") == 0U);
    f.bus.Write32(tail, 0xDEADBEEFU, 1U);
    AuditGl(f, "glGetSamplerParameteriv", {sampler, 0x1004, tail.Value()});
    CHECK(AuditGl(f, "glGetError") == 0x0500U); CHECK(f.bus.Read32(tail, 1U) == 0xDEADBEEFU);
    AuditGl(f, "glSamplerParameterfv", {sampler, 0x1004, tail.Value()});
    CHECK(AuditGl(f, "glGetError") == 0x0500U);
    AuditGl(f, "glGetTexParameterfv", {0x0DE1, 0x1004, tail.Value()});
    CHECK(AuditGl(f, "glGetError") == 0x0500U); CHECK(f.bus.Read32(tail, 1U) == 0xDEADBEEFU);
    const auto tex = AuditName(f, "glGenTextures"); AuditGl(f, "glBindTexture", {0x0DE1, tex});
    f.bus.Write32(f.output.Add(256), 0x812FU, 1U);
    AuditGl(f, "glTexParameteriv", {0x0DE1, 0x8072, f.output.Add(256).Value()});
    AuditGl(f, "glGetTexParameteriv", {0x0DE1, 0x8072, tail.Value()}); CHECK(f.bus.Read32(tail, 1U) == 0x812FU);
    AuditGl(f, "glTexParameterf", {0x0DE1, 0x813A, 0x40600000U});
    AuditGl(f, "glGetTexParameterfv", {0x0DE1, 0x813A, tail.Value()}); CHECK(f.bus.Read32(tail, 1U) == 0x40600000U);
    CHECK(AuditGl(f, "glGetError") == 0U);
    const auto compile = [&](const std::uint32_t type, const std::string_view source) {
        const auto shader = AuditGl(f, "glCreateShader", {type});
        WriteGuestString(f, f.output.Add(512), source);
        f.bus.Write32(f.output.Add(256), f.output.Add(512).Value(), 1);
        AuditGl(f, "glShaderSource", {shader, 1, f.output.Add(256).Value(), 0});
        AuditGl(f, "glCompileShader", {shader});
        AuditGl(f, "glGetShaderiv", {shader, 0x8B81, f.output.Value()});
        REQUIRE(f.bus.Read32(f.output, 1) == 1U);
        return shader;
    };
    const auto vertex = compile(0x8B31, "#version 300 es\nlayout(std140) uniform U{vec4 a;vec4 b;};uniform mat4 m;void main(){gl_Position=m*(a+b);}");
    const auto fragment = compile(0x8B30, "#version 300 es\nprecision highp float;out vec4 c;void main(){c=vec4(1);}");
    const auto program = AuditGl(f, "glCreateProgram");
    AuditGl(f, "glAttachShader", {program, vertex});
    AuditGl(f, "glAttachShader", {program, fragment});
    AuditGl(f, "glLinkProgram", {program});
    AuditGl(f, "glGetProgramiv", {program, 0x8B82, f.output.Value()});
    REQUIRE(f.bus.Read32(f.output, 1) == 1U);
    AuditGl(f, "glGetProgramInfoLog", {program, 32, 0, f.output.Add(800).Value()});
    WriteGuestString(f, f.output.Add(256), "U");
    const auto block = AuditGl(f, "glGetUniformBlockIndex", {program, f.output.Add(256).Value()});
    AuditGl(f, "glGetActiveUniformBlockiv", {program, block, 0x8A42, f.output.Value()});
    REQUIRE(f.bus.Read32(f.output, 1) == 2U);
    f.bus.Write32(f.output.Add(8), 0xDEADBEEFU, 1);
    AuditGl(f, "glGetActiveUniformBlockiv", {program, block, 0x8A43, f.output.Value()});
    CHECK(f.bus.Read32(f.output, 1) != f.bus.Read32(f.output.Add(4), 1));
    CHECK(f.bus.Read32(f.output.Add(8), 1) == 0xDEADBEEFU);
    CHECK(AuditGl(f, "glGetError") == 0U);
    const auto count = AuditInteger(f, 0x821D);
    CHECK(count >= 3U);
    CHECK(AuditGl(f, "glGetStringi", {0x1F03, count}) == 0U);
    CHECK(AuditGl(f, "glGetError") == 0x0501U);
    WriteGuestString(f, f.output.Add(256), "m");
    const auto location = AuditGl(f, "glGetUniformLocation", {program, f.output.Add(256).Value()});
    REQUIRE(location != 0xFFFFFFFFU);
    const auto shared = AuditContext(f, 3U, context);
    AuditBind(f, shared, surface);
    f.bus.Write32(f.output.Add(64), 0xABCDEF12U, 1U);
    AuditGl(f, "glGetUniformfv", {program, location, f.output.Value()});
    for (std::uint32_t i = 0; i < 16; ++i) CHECK(f.bus.Read32(f.output.Add(i * 4), 1U) == 0U);
    CHECK(f.bus.Read32(f.output.Add(64), 1U) == 0xABCDEF12U);
    CHECK(AuditGl(f, "glGetError") == 0U);
}

TEST_CASE("BND34 ES3 buffers VAOs pixel stores and map identities are isolated") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto a = AuditContext(f), b = AuditContext(f), surface = AuditSurface(f);
    AuditBind(f, a, surface);
    const auto buffer_a = AuditName(f, "glGenBuffers");
    AuditGl(f, "glBindBuffer", {0x8A11, buffer_a});
    AuditGl(f, "glBufferData", {0x8A11, 64, 0, 0x88E8});
    const auto mapped_a = AuditGl(f, "glMapBufferRange", {0x8A11, 0, 64, 2});
    REQUIRE(mapped_a != 0U);
    f.bus.Write32(ogplay::memory::GuestAddress{mapped_a}, 0x12345678U, 1);
    AuditBind(f, b, surface);
    const auto buffer_b = AuditName(f, "glGenBuffers");
    REQUIRE(buffer_a == buffer_b);
    AuditGl(f, "glBindBuffer", {0x8A11, buffer_b});
    AuditGl(f, "glBufferData", {0x8A11, 64, 0, 0x88E8});
    const auto mapped_b = AuditGl(f, "glMapBufferRange", {0x8A11, 0, 64, 2});
    REQUIRE(mapped_b != 0U); CHECK(mapped_a != mapped_b);
    CHECK(AuditGl(f, "glUnmapBuffer", {0x8A11}) == 1U);
    AuditBind(f, a, surface);
    CHECK(AuditGl(f, "glUnmapBuffer", {0x8A11}) == 1U);
    const auto vao_a = AuditName(f, "glGenVertexArrays"), vao_b = AuditName(f, "glGenVertexArrays");
    AuditGl(f, "glBindVertexArray", {vao_a});
    AuditGl(f, "glBindBuffer", {0x8893, buffer_a});
    AuditGl(f, "glEnableVertexAttribArray", {0});
    AuditGl(f, "glBindVertexArray", {vao_b});
    CHECK(AuditInteger(f, 0x8895) == 0U);
    AuditGl(f, "glBindVertexArray", {vao_a});
    CHECK(AuditInteger(f, 0x8895) == buffer_a);
    AuditGl(f, "glGetVertexAttribiv", {0, 0x8622, f.output.Value()});
    CHECK(f.bus.Read32(f.output, 1) == 1U);
    AuditGl(f, "glBindBuffer", {0x88EC, buffer_a});
    AuditGl(f, "glBufferData", {0x88EC, 64, 0, 0x88E8});
    const auto texture = AuditName(f, "glGenTextures");
    AuditGl(f, "glBindTexture", {0x806F, texture});
    AuditGl(f, "glTexImage3D", {0x806F, 0, 0x8058, 1, 1, 1, 0, 0x1908, 0x1401, 4});
    CHECK(AuditGl(f, "glGetError") == 0U);
    AuditGl(f, "glBindBuffer", {0x88EC, 0});
    AuditGl(f, "glPixelStorei", {0x0D02, 4});
    AuditGl(f, "glPixelStorei", {0x0D03, 1});
    AuditGl(f, "glPixelStorei", {0x0D04, 1});
    AuditGl(f, "glClearColor", {0x3F800000, 0, 0, 0x3F800000});
    AuditGl(f, "glClear", {0x4000});
    for (std::uint32_t i = 0; i < 64; i += 4) f.bus.Write32(f.output.Add(256 + i), 0xAABBCCDDU, 1);
    AuditGl(f, "glReadPixels", {0, 0, 1, 2, 0x1908, 0x1401, f.output.Add(256).Value()});
    CHECK(f.bus.Read32(f.output.Add(256), 1) == 0xAABBCCDDU);
    CHECK(f.bus.Read32(f.output.Add(276), 1) == 0xFF0000FFU);
    CHECK(f.bus.Read32(f.output.Add(280), 1) == 0xAABBCCDDU);
    CHECK(f.bus.Read32(f.output.Add(292), 1) == 0xFF0000FFU);
    CHECK(AuditGl(f, "glGetError") == 0U);
}
