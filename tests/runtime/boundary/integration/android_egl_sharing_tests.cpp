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

TEST_CASE("Java EGL bridge and native EGL share pbuffer context registry") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    constexpr std::uint64_t thread_id = 0x33U;
    const auto attributes = fixture.output.Add(0x100U);
    fixture.bus.Write32(attributes, 0x3057U, 1U);
    fixture.bus.Write32(attributes.Add(4U), 2U, 1U);
    fixture.bus.Write32(attributes.Add(8U), 0x3056U, 1U);
    fixture.bus.Write32(attributes.Add(12U), 2U, 1U);
    fixture.bus.Write32(attributes.Add(16U), 0x3038U, 1U);
    const auto context_attributes = fixture.output.Add(0x140U);
    fixture.bus.Write32(context_attributes, 0x3098U, 1U);
    fixture.bus.Write32(context_attributes.Add(4U), 2U, 1U);
    fixture.bus.Write32(context_attributes.Add(8U), 0x3038U, 1U);

    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglGetDisplay", std::array{0U}, thread_id) == 1U);
    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglInitialize", std::array{1U, 0U, 0U}, thread_id) == 1U);
    const auto surface = fixture.boundary.InvokeManagedEgl(
        "eglCreatePbufferSurface",
        std::array{1U, 2U, attributes.Value()}, thread_id);
    REQUIRE(surface != 0U);
    const auto context = fixture.boundary.InvokeManagedEgl(
        "eglCreateContext",
        std::array{1U, 2U, 0U, context_attributes.Value()}, thread_id);
    REQUIRE(context != 0U);
    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglMakeCurrent",
              std::array{1U, surface, surface, context}, thread_id) == 1U);

    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglGetCurrentContext",
                           {}, thread_id) == context);
    CHECK(fixture.boundary.InvokeManagedGles(
              ogplay::gles::GlesApi::gles2, "glClearColor",
              std::array{std::bit_cast<std::uint32_t>(0.25F),
                         std::bit_cast<std::uint32_t>(0.5F),
                         std::bit_cast<std::uint32_t>(0.75F),
                         std::bit_cast<std::uint32_t>(1.0F)},
              thread_id) == 0U);
    CHECK(fixture.boundary.InvokeManagedGles(
              ogplay::gles::GlesApi::gles2, "glClear",
              std::array{0x00004000U}, thread_id) == 0U);
    const auto pixels = fixture.output.Add(0x180U);
    CHECK(fixture.boundary.InvokeManagedGles(
              ogplay::gles::GlesApi::gles2, "glReadPixels",
              std::array{0U, 0U, 1U, 1U, 0x1908U, 0x1401U,
                         pixels.Value()}, thread_id) == 0U);
    std::array<std::byte, 4> rgba{};
    fixture.memory.Read(pixels, rgba, 1U);
    CHECK(std::to_integer<std::uint8_t>(rgba[0]) ==
          doctest::Approx(64).epsilon(0.04));
    CHECK(std::to_integer<std::uint8_t>(rgba[1]) ==
          doctest::Approx(128).epsilon(0.04));
    CHECK(std::to_integer<std::uint8_t>(rgba[2]) ==
          doctest::Approx(191).epsilon(0.04));

    const auto shared = FastBoundaryCall(
        fixture, "libEGL.so", "eglCreateContext",
        {1U, 2U, context, context_attributes.Value()}, thread_id);
    REQUIRE(shared != 0U);
    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglMakeCurrent", std::array{1U, 0U, 0U, 0U},
              thread_id) == 1U);
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglDestroyContext",
                           {1U, shared}, thread_id) == 1U);
    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglDestroyContext", std::array{1U, context}, thread_id) == 1U);
    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglDestroySurface", std::array{1U, surface}, thread_id) == 1U);
    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglTerminate", std::array{1U}, thread_id) == 1U);
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("Android EGL registry isolates pbuffer content and honors share groups") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);
    const auto surface_attributes = fixture.output;
    const auto make_surface = [&](const std::uint32_t width,
                                  const std::uint32_t height) {
        fixture.bus.Write32(surface_attributes, 0x3057U, 1U);
        fixture.bus.Write32(surface_attributes.Add(4U), width, 1U);
        fixture.bus.Write32(surface_attributes.Add(8U), 0x3056U, 1U);
        fixture.bus.Write32(surface_attributes.Add(12U), height, 1U);
        fixture.bus.Write32(surface_attributes.Add(16U), 0x3038U, 1U);
        return fixture.Call("libEGL.so", "eglCreatePbufferSurface",
                            {1U, 2U, surface_attributes.Value()});
    };
    const auto first_surface = make_surface(4U, 3U);
    const auto second_surface = make_surface(2U, 2U);
    REQUIRE(first_surface == 3U);
    REQUIRE(second_surface == 4U);
    const auto context_attributes = fixture.output.Add(0x40U);
    fixture.bus.Write32(context_attributes, 0x3098U, 1U);
    fixture.bus.Write32(context_attributes.Add(4U), 2U, 1U);
    fixture.bus.Write32(context_attributes.Add(8U), 0x3038U, 1U);
    const auto first_context = fixture.Call(
        "libEGL.so", "eglCreateContext",
        {1U, 2U, 0U, context_attributes.Value()});
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, first_surface, first_surface, first_context}) == 1U);

    const auto clear = [&](const float red, const float green) {
        CHECK(fixture.Call(
                  "libGLESv2.so", "glClearColor",
                  {std::bit_cast<std::uint32_t>(red),
                   std::bit_cast<std::uint32_t>(green), 0U,
                   std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
        CHECK(fixture.Call("libGLESv2.so", "glClear", {0x00004000U}) == 0U);
    };
    const auto pixel = fixture.output.Add(0x100U);
    const auto read_pixel = [&]() {
        fixture.bus.Write32(fixture.stack, 0x1908U, 1U);
        fixture.bus.Write32(fixture.stack.Add(4U), 0x1401U, 1U);
        fixture.bus.Write32(fixture.stack.Add(8U), pixel.Value(), 1U);
        CHECK(fixture.Call("libGLESv2.so", "glReadPixels",
                           {0U, 0U, 1U, 1U}) == 0U);
        std::array<std::byte, 4> rgba{};
        fixture.memory.Read(pixel, rgba, 1U);
        return rgba;
    };
    clear(1.0F, 0.0F);
    const auto texture_output = fixture.output.Add(0x120U);
    CHECK(fixture.Call("libGLESv2.so", "glGenTextures",
                       {1U, texture_output.Value()}) == 0U);
    const auto texture = fixture.bus.Read32(texture_output, 1U);
    REQUIRE(texture != 0U);
    CHECK(fixture.Call("libGLESv2.so", "glBindTexture",
                       {0x0DE1U, texture}) == 0U);
    const auto viewport_output = fixture.output.Add(0x140U);
    CHECK(fixture.Call("libGLESv2.so", "glViewport",
                       {1U, 0U, 3U, 3U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0BA2U, viewport_output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(viewport_output, 1U) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glBindTexture",
                       {0xDEADU, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glEnable", {0x0BE2U}) == 0U);

    const auto shared_context = fixture.Call(
        "libEGL.so", "eglCreateContext",
        {1U, 2U, first_context, context_attributes.Value()});
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, second_surface, second_surface, shared_context}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glIsTexture", {texture}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glIsEnabled", {0x0BE2U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0BA2U, viewport_output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(viewport_output.Add(8U), 1U) == 2U);
    clear(0.0F, 1.0F);
    const auto green = read_pixel();
    CHECK(std::to_integer<std::uint8_t>(green[0]) == 0U);
    CHECK(std::to_integer<std::uint8_t>(green[1]) == 255U);

    const auto isolated_context = fixture.Call(
        "libEGL.so", "eglCreateContext",
        {1U, 2U, 0U, context_attributes.Value()});
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, second_surface, second_surface,
                          isolated_context}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glIsTexture", {texture}) == 0U);

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, first_surface, first_surface, first_context}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0x0500U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0BA2U, viewport_output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(viewport_output, 1U) == 1U);
    CHECK(fixture.bus.Read32(viewport_output.Add(8U), 1U) == 3U);
    CHECK(fixture.Call("libGLESv2.so", "glIsEnabled", {0x0BE2U}) == 1U);
    const auto red = read_pixel();
    CHECK(std::to_integer<std::uint8_t>(red[0]) == 255U);
    CHECK(std::to_integer<std::uint8_t>(red[1]) == 0U);

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, first_surface, second_surface,
                          shared_context}) == 1U);
    clear(0.0F, 0.0F);
    const auto read_surface_green = read_pixel();
    CHECK(std::to_integer<std::uint8_t>(read_surface_green[1]) == 255U);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, first_surface, first_surface,
                          shared_context}) == 1U);
    const auto draw_surface_black = read_pixel();
    CHECK(std::to_integer<std::uint8_t>(draw_surface_black[0]) == 0U);
    CHECK(std::to_integer<std::uint8_t>(draw_surface_black[1]) == 0U);

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, second_surface, second_surface,
                          shared_context}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglDestroyContext",
                       {1U, first_context}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glIsTexture", {texture}) == 1U);
}

TEST_CASE("Android EGL restores GLES1 matrices with the current context") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);
    const auto attributes = fixture.output;
    fixture.bus.Write32(attributes, 0x3057U, 1U);
    fixture.bus.Write32(attributes.Add(4U), 4U, 1U);
    fixture.bus.Write32(attributes.Add(8U), 0x3056U, 1U);
    fixture.bus.Write32(attributes.Add(12U), 4U, 1U);
    fixture.bus.Write32(attributes.Add(16U), 0x3038U, 1U);
    const auto surface = fixture.Call("libEGL.so", "eglCreatePbufferSurface",
                                      {1U, 2U, attributes.Value()});
    const auto context_a = fixture.Call("libEGL.so", "eglCreateContext",
                                        {1U, 2U, 0U, 0U});
    const auto context_b = fixture.Call("libEGL.so", "eglCreateContext",
                                        {1U, 2U, 0U, 0U});
    REQUIRE(surface != 0U);
    REQUIRE(context_a != 0U);
    REQUIRE(context_b != 0U);
    const auto matrix = fixture.output.Add(64U);
    const auto translation_x = [&] {
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetFloatv",
                           {0x0BA6U, matrix.Value()}) == 0U);
        return std::bit_cast<float>(fixture.bus.Read32(matrix.Add(48U), 1U));
    };

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, surface, surface, context_a}) == 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glMatrixMode", {0x1700U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glLoadIdentity") == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glTranslatef",
              {std::bit_cast<std::uint32_t>(1.0F), 0U, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glShadeModel", {0x1D00U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0BA1U}) == 0U);
    CHECK(translation_x() == doctest::Approx(1.0F));

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, surface, surface, context_b}) == 1U);
    CHECK(translation_x() == doctest::Approx(0.0F));
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                       {0x0B54U, matrix.Value()}) == 0U);
    CHECK(fixture.bus.Read32(matrix, 1U) == 0x1D01U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glIsEnabled", {0x0BA1U}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glTranslatef",
              {std::bit_cast<std::uint32_t>(4.0F), 0U, 0U}) == 0U);
    CHECK(translation_x() == doctest::Approx(4.0F));

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, surface, surface, context_a}) == 1U);
    CHECK(translation_x() == doctest::Approx(1.0F));
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                       {0x0B54U, matrix.Value()}) == 0U);
    CHECK(fixture.bus.Read32(matrix, 1U) == 0x1D00U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glIsEnabled", {0x0BA1U}) == 1U);
}

TEST_CASE("BND34 EGL swap presents draw surface and preserves read framebuffer") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1, 0, 0}) == 1U);
    const auto window = f.Call("libEGL.so", "eglCreateWindowSurface", {1, 2, 3, 0}); REQUIRE(window != 0U);
    const auto read_surface = AuditSurface(f), context = AuditContext(f);
    AuditBind(f, context, window, read_surface);
    AuditGl(f, "glClearColor", {0x3F800000U, 0, 0, 0x3F800000U}); AuditGl(f, "glClear", {0x4000});
    const auto texture = AuditName(f, "glGenTextures"), framebuffer = AuditName(f, "glGenFramebuffers");
    AuditGl(f, "glBindTexture", {0x0DE1, texture}); f.bus.Write32(f.output.Add(700), 0xFF00FF00U, 1U);
    AuditGl(f, "glTexImage2D", {0x0DE1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, f.output.Add(700).Value()});
    AuditGl(f, "glBindFramebuffer", {0x8CA8, framebuffer});
    AuditGl(f, "glFramebufferTexture2D", {0x8CA8, 0x8CE0, 0x0DE1, texture, 0});
    CHECK(AuditPixel(f) == 0xFF00FF00U);
    REQUIRE(f.Call("libEGL.so", "eglSwapBuffers", {1, window}) == 1U);
    const auto frame = f.boundary.TakeLatestFrame(); REQUIRE(frame.has_value());
    REQUIRE(frame->rgba8.size() >= 4); CHECK(frame->rgba8[0] == 255U); CHECK(frame->rgba8[1] == 0U);
    CHECK(AuditInteger(f, 0x8CAA) == framebuffer); CHECK(AuditInteger(f, 0x8CA6) == 0U);
    CHECK(AuditPixel(f) == 0xFF00FF00U);
}

TEST_CASE("BND34 EGL separates context state surface content and read surface") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto a = AuditContext(f), b = AuditContext(f);
    const auto x = AuditSurface(f), y = AuditSurface(f);
    AuditBind(f, a, x);
    AuditGl(f, "glEnable", {0x0BE2});
    AuditGl(f, "glClearColor", {0x3F800000, 0, 0, 0x3F800000});
    AuditGl(f, "glClear", {0x4000});
    AuditBind(f, a, y);
    CHECK(AuditGl(f, "glIsEnabled", {0x0BE2}) == 1U);
    AuditGl(f, "glClearColor", {0, 0x3F800000, 0, 0x3F800000});
    AuditGl(f, "glClear", {0x4000});
    AuditBind(f, b, x);
    CHECK(AuditGl(f, "glIsEnabled", {0x0BE2}) == 0U);
    CHECK(AuditPixel(f) == 0xFF0000FFU);
    AuditBind(f, a, y, x);
    CHECK(AuditPixel(f) == 0xFF0000FFU);
    const auto texture = AuditName(f, "glGenTextures");
    AuditGl(f, "glBindTexture", {0x0DE1, texture});
    AuditGl(f, "glCopyTexImage2D", {0x0DE1, 0, 0x1908, 0, 0, 1, 1, 0});
    const auto framebuffer = AuditName(f, "glGenFramebuffers");
    AuditGl(f, "glBindFramebuffer", {0x8D40, framebuffer});
    AuditGl(f, "glFramebufferTexture2D", {0x8D40, 0x8CE0, 0x0DE1, texture, 0});
    CHECK(AuditPixel(f) == 0xFF0000FFU);
    AuditGl(f, "glBindFramebuffer", {0x8D40, 0});
    f.bus.Write32(f.output.Add(256), 0xFF000000U, 1);
    AuditGl(f, "glTexSubImage2D", {0x0DE1, 0, 0, 0, 1, 1, 0x1908, 0x1401, f.output.Add(256).Value()});
    AuditGl(f, "glCopyTexSubImage2D", {0x0DE1, 0, 0, 0, 0, 0, 1, 1});
    CHECK(f.Call("libEGL.so", "eglGetCurrentSurface", {0x3059}) == y);
    CHECK(f.Call("libEGL.so", "eglGetCurrentSurface", {0x305A}) == x);
    AuditGl(f, "glBindFramebuffer", {0x8D40, framebuffer});
    CHECK(AuditPixel(f) == 0xFF0000FFU);
}

TEST_CASE("BND34 EGL share group survives reverse bind order and source deletion") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto a = AuditContext(f), b = AuditContext(f, 3U, a);
    const auto surface = AuditSurface(f);
    AuditBind(f, b, surface);
    const auto texture = AuditName(f, "glGenTextures");
    AuditGl(f, "glBindTexture", {0x0DE1, texture});
    AuditBind(f, a, surface);
    CHECK(AuditGl(f, "glIsTexture", {texture}) == 1U);
    REQUIRE(f.Call("libEGL.so", "eglMakeCurrent", {1, 0, 0, 0}) == 1U);
    REQUIRE(f.Call("libEGL.so", "eglDestroyContext", {1, a}) == 1U);
    AuditBind(f, b, surface);
    CHECK(AuditGl(f, "glIsTexture", {texture}) == 1U);
}

TEST_CASE("BND34 EGL fences images and texture pbuffers use real ANGLE objects") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    const auto egl = [&](const std::string_view name, const std::initializer_list<std::uint32_t> args) {
        const auto entry = f.boundary.Symbols().Lookup("libEGL.so", name);
        REQUIRE(entry.has_value());
        return BoundaryCallAddress(f, entry->Value(), {args.begin(), args.size()});
    };
    REQUIRE(egl("eglInitialize", {1}) == 1U);
    const auto context = AuditContext(f), surface = AuditSurface(f);
    AuditBind(f, context, surface);
    const auto extension_address = ogplay::memory::GuestAddress{egl("eglQueryString", {1, 0x3055})};
    std::string extensions;
    for (std::uint32_t i = 0; ; ++i) {
        const auto byte = f.bus.Read8(extension_address.Add(i), 1);
        if (byte == 0U) break;
        extensions.push_back(static_cast<char>(byte));
    }
    const bool reusable = extensions.find("EGL_KHR_reusable_sync") != std::string::npos;
    const auto sync_type = reusable ? 0x30FAU : 0x30F9U;
    const auto sync = egl("eglCreateSyncKHR", {1, sync_type, 0});
    INFO("EGL create sync error: ", egl("eglGetError", {}));
    REQUIRE(sync != 0U);
    if (reusable) {
        CHECK(egl("eglClientWaitSyncKHR", {1, sync, 0, 0, 0, 0}) == 0x30F5U);
        CHECK(egl("eglSignalSyncKHR", {1, sync, 0x30F2}) == 1U);
    }
    CHECK(egl("eglGetSyncAttribKHR", {1, sync, 0x30F7, f.output.Value()}) == 1U);
    CHECK(f.bus.Read32(f.output, 1) == sync_type);
    CHECK(egl("eglClientWaitSyncKHR", {1, sync, 1, 0, 0xFFFFFFFFU, 0xFFFFFFFFU}) == 0x30F6U);
    CHECK(egl("eglDestroySyncKHR", {1, sync}) == 1U);
    CHECK(egl("eglDestroySyncKHR", {1, sync}) == 0U);
    CHECK(egl("eglGetError", {}) == 0x300CU);
    const auto texture = AuditName(f, "glGenTextures");
    AuditGl(f, "glBindTexture", {0x0DE1, texture});
    f.bus.Write32(f.output.Add(256), 0xFF112233U, 1);
    AuditGl(f, "glTexImage2D", {0x0DE1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, f.output.Add(256).Value()});
    // An EGL texture image requires a complete source texture.
    AuditGl(f, "glTexParameteri", {0x0DE1, 0x2801, 0x2600});
    AuditGl(f, "glTexParameteri", {0x0DE1, 0x2800, 0x2600});
    f.bus.Write32(f.output, 0x30D2, 1); f.bus.Write32(f.output.Add(4), 1, 1);
    f.bus.Write32(f.output.Add(8), 0x3038, 1);
    const auto image = egl("eglCreateImageKHR", {1, context, 0x30B1, texture, f.output.Value()});
    const auto framebuffer = AuditName(f, "glGenFramebuffers");
    if (extensions.find("EGL_KHR_gl_texture_2D_image") != std::string::npos) {
        REQUIRE(image != 0U);
        const auto sibling = AuditName(f, "glGenTextures");
        AuditGl(f, "glBindTexture", {0x0DE1, sibling});
        AuditGl(f, "glEGLImageTargetTexture2DOES", {0x0DE1, image});
        REQUIRE(AuditGl(f, "glGetError") == 0U);
        CHECK(egl("eglDestroyImageKHR", {1, image}) == 1U);
        AuditGl(f, "glBindFramebuffer", {0x8D40, framebuffer});
        AuditGl(f, "glFramebufferTexture2D", {0x8D40, 0x8CE0, 0x0DE1, sibling, 0});
        CHECK(AuditPixel(f) == 0xFF112233U);
        AuditGl(f, "glBindFramebuffer", {0x8D40, 0});
    } else {
        CHECK(image == 0U);
        CHECK(egl("eglGetError", {}) == 0x300CU);
    }
    const std::array<std::uint32_t, 11> attributes{0x3057, 4, 0x3056, 4, 0x3080, 0x305E,
                                                0x3081, 0x305F, 0x3082, 0, 0x3038};
    for (std::size_t i = 0; i < attributes.size(); ++i) f.bus.Write32(f.output.Add(i * 4), attributes[i], 1);
    const auto textured = egl("eglCreatePbufferSurface", {1, 2, f.output.Value()});
    REQUIRE(textured != 0U);
    AuditBind(f, context, textured);
    AuditGl(f, "glClearColor", {0, 0x3F800000, 0, 0x3F800000});
    AuditGl(f, "glClear", {0x4000});
    AuditBind(f, context, surface);
    const auto bound_texture = AuditName(f, "glGenTextures");
    AuditGl(f, "glBindTexture", {0x0DE1, bound_texture});
    REQUIRE(egl("eglBindTexImage", {1, textured, 0x3084}) == 1U);
    AuditGl(f, "glBindFramebuffer", {0x8D40, framebuffer});
    AuditGl(f, "glFramebufferTexture2D", {0x8D40, 0x8CE0, 0x0DE1, bound_texture, 0});
    CHECK(AuditPixel(f) == 0xFF00FF00U);
    CHECK(egl("eglReleaseTexImage", {1, textured, 0x3084}) == 1U);
    CHECK(egl("eglSwapInterval", {1, 99}) == 1U);
    CHECK(egl("eglSwapInterval", {1, 0xFFFFFFFFU}) == 1U);
}

TEST_CASE("BND49 EGL minimum colors select distinct RGB storage with truthful alpha") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1, 0, 0}) == 1U);
    const auto attributes = f.output.Add(256), configs = f.output.Add(320), count = f.output.Add(352);
    const std::array<std::uint32_t, 11> minimum{0x3024, 4, 0x3023, 4, 0x3022, 4,
                                             0x3040, 4, 0x3027, 0x3038, 0x3038};
    for (std::size_t index = 0; index < minimum.size(); ++index)
        f.bus.Write32(attributes.Add(index * 4U), minimum[index], 1U);
    f.bus.Write32(f.stack, count.Value(), 1U);
    REQUIRE(f.Call("libEGL.so", "eglChooseConfig", {1, attributes.Value(), 0, 0}) == 1U);
    REQUIRE(f.bus.Read32(count, 1U) >= 2U);
    REQUIRE(f.Call("libEGL.so", "eglChooseConfig", {1, attributes.Value(), configs.Value(), 2}) == 1U);
    REQUIRE(f.bus.Read32(count, 1U) == 2U);
    const auto rgba = f.bus.Read32(configs, 1U), rgb = f.bus.Read32(configs.Add(4), 1U);
    REQUIRE(rgb != rgba);
    const auto query = [&](std::uint32_t config, std::uint32_t attribute) {
        REQUIRE(f.Call("libEGL.so", "eglGetConfigAttrib", {1, config, attribute, count.Value()}) == 1U);
        return f.bus.Read32(count, 1U);
    };
    for (const auto attribute : {0x3024U, 0x3023U, 0x3022U}) {
        CHECK(query(rgb, attribute) == 8U);
        CHECK(query(rgba, attribute) == 8U);
    }
    CHECK(query(rgb, 0x3021) == 0U);
    CHECK(query(rgb, 0x3020) == 24U);
    CHECK(query(rgba, 0x3021) == 8U);
    // A later RGBA-only selection must not change either config's facts.
    f.bus.Write32(attributes.Add(40), 0x3021, 1U);
    f.bus.Write32(attributes.Add(44), 8, 1U);
    f.bus.Write32(attributes.Add(48), 0x3038, 1U);
    REQUIRE(FastBoundaryCall(f, "libEGL.so", "eglChooseConfig",
                            {1, attributes.Value(), configs.Value(), 2}) == 1U);
    CHECK(f.bus.Read32(count, 1U) == 1U);
    CHECK(f.bus.Read32(configs, 1U) == rgba);
    CHECK(query(rgb, 0x3021) == 0U);
    const auto window = f.Call("libEGL.so", "eglCreateWindowSurface", {1, rgb, 3, 0}); REQUIRE(window != 0U);
    f.bus.Write32(attributes, 0x3098, 1U); f.bus.Write32(attributes.Add(4), 2, 1U); f.bus.Write32(attributes.Add(8), 0x3038, 1U);
    const auto context = f.Call("libEGL.so", "eglCreateContext", {1, rgb, 0, attributes.Value()}); REQUIRE(context != 0U);
    const auto rgba_window = f.Call("libEGL.so", "eglCreateWindowSurface", {1, rgba, 3, 0}); REQUIRE(rgba_window != 0U);
    CHECK(f.Call("libEGL.so", "eglMakeCurrent", {1, rgba_window, rgba_window, context}) == 0U);
    CHECK(f.Call("libEGL.so", "eglGetError") == 0x3009U);
    REQUIRE(f.Call("libEGL.so", "eglDestroySurface", {1, rgba_window}) == 1U);
    AuditBind(f, context, window, window);
    for (const auto [attribute, value] : {std::pair{0x0D52U, 8U}, {0x0D55U, 0U}, {0x0D56U, query(rgb, 0x3025)}, {0x0D57U, query(rgb, 0x3026)}}) {
        AuditGl(f, "glGetIntegerv", {attribute, count.Value()}); CHECK(f.bus.Read32(count, 1U) == value);
    }
    AuditGl(f, "glClearColor", {0x3E800000U, 0x3F000000U, 0x3F400000U, 0}); AuditGl(f, "glClear", {0x4000});
    CHECK((AuditPixel(f) >> 24U) == 255U);
    REQUIRE(f.Call("libEGL.so", "eglSwapBuffers", {1, window}) == 1U);
    const auto published = f.boundary.TakeLatestFrame(); REQUIRE(published);
    CHECK(published->rgba8[0] == doctest::Approx(64).epsilon(.04));
    CHECK(published->rgba8[3] == 255U);
    CHECK(query(rgba, 0x3021) == 8U); // A second choose did not mutate another config.
    REQUIRE(f.Call("libEGL.so", "eglMakeCurrent", {1, 0, 0, 0}) == 1U);
    REQUIRE(f.Call("libEGL.so", "eglDestroyContext", {1, context}) == 1U);
    REQUIRE(f.Call("libEGL.so", "eglDestroySurface", {1, window}) == 1U);
}

TEST_CASE("BND53 EGL registry exposes RGB565 storage consistently and preserves current lifetime") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so","eglInitialize",{1,0,0})==1U);
    const auto attrs=f.output.Add(256),configs=f.output.Add(384),count=f.output.Add(448);
    const std::array<std::uint32_t,11> spec{0x3024,5,0x3023,4,0x3022,5,0x3040,4,0x3033,4,0x3038};
    for(std::size_t i=0;i<spec.size();++i) f.bus.Write32(attrs.Add(static_cast<std::uint32_t>(i*4)),spec[i],1U);
    f.bus.Write32(f.stack,count.Value(),1U);
    REQUIRE(f.Call("libEGL.so","eglChooseConfig",{1,attrs.Value(),configs.Value(),8})==1U);
    const auto total=f.bus.Read32(count,1U);REQUIRE(total>0);
    const auto query=[&](std::uint32_t cfg,std::uint32_t p){
        REQUIRE(f.Call("libEGL.so","eglGetConfigAttrib",{1,cfg,p,count.Value()})==1U);
        return f.bus.Read32(count,1U);
    };
    std::uint32_t packed{};
    for(std::uint32_t i=0;i<total;++i){const auto cfg=f.bus.Read32(configs.Add(i*4),1U);
        if(query(cfg,0x3024)==5 && query(cfg,0x3023)==6 && query(cfg,0x3022)==5 && query(cfg,0x3021)==0)packed=cfg;
    }
#if defined(__APPLE__)
    REQUIRE(packed!=0);
#else
    if(!packed)return;
#endif
    CHECK(query(packed,0x3020)==16);
    f.bus.Write32(attrs,0x3098,1U);f.bus.Write32(attrs.Add(4),2,1U);f.bus.Write32(attrs.Add(8),0x3038,1U);
    const auto ctx=f.Call("libEGL.so","eglCreateContext",{1,packed,0,attrs.Value()});REQUIRE(ctx);
    const auto surface=f.Call("libEGL.so","eglCreateWindowSurface",{1,packed,3,0});REQUIRE(surface);
    AuditBind(f,ctx,surface,surface);
    for(const auto [p,value]:{std::pair{0x0D52U,5U},std::pair{0x0D53U,6U},std::pair{0x0D54U,5U},std::pair{0x0D55U,0U}}){
        AuditGl(f,"glGetIntegerv",{p,count.Value()});CHECK(f.bus.Read32(count,1U)==value);
    }
    AuditGl(f,"glClearColor",{0x3dcccccdu,0x3e4ccccdu,0x3e99999au,0});AuditGl(f,"glClear",{0x4000});
    const auto pixel=AuditPixel(f);CHECK((pixel>>24)==255);
    REQUIRE(f.Call("libEGL.so","eglSwapBuffers",{1,surface})==1U);
    const auto frame=f.boundary.TakeLatestFrame();REQUIRE(frame);CHECK(frame->rgba8[3]==255);
    CHECK(frame->rgba8[0]>=24);CHECK(frame->rgba8[0]<=25);
    REQUIRE(f.Call("libEGL.so","eglDestroySurface",{1,surface})==1U);
    CHECK(f.Call("libEGL.so","eglGetCurrentSurface",{0x3059})==surface);
    REQUIRE(f.Call("libEGL.so","eglMakeCurrent",{1,0,0,0})==1U);
    REQUIRE(f.Call("libEGL.so","eglDestroyContext",{1,ctx})==1U);
}
