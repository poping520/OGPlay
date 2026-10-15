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

TEST_CASE("Android boundary supersamples without changing guest surface size") {
    CHECK_THROWS_AS(BoundaryFixture(0), std::invalid_argument);
    CHECK_THROWS_AS(BoundaryFixture(5), std::invalid_argument);
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;

    BoundaryFixture fixture(2);
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);
    CHECK(fixture.Call("libEGL.so", "eglQuerySurface",
                       {1, 3, 0x3057U, fixture.output.Value()}) == 1);
    CHECK(fixture.bus.Read32(fixture.output) == 4);
    CHECK(fixture.Call("libEGL.so", "eglQuerySurface",
                       {1, 3, 0x3056U, fixture.output.Value()}) == 1);
    CHECK(fixture.bus.Read32(fixture.output) == 3);

    const auto targets = fixture.boundary.RenderTargets();
    REQUIRE(targets.size() == 1);
    CHECK(targets[0].width == 8);
    CHECK(targets[0].height == 6);

    static_cast<void>(fixture.Call("libGLESv2.so", "glViewport", {0, 0, 4, 3}));
    CHECK_THROWS_AS(
        fixture.Call("libGLESv2.so", "glViewport", {0x40000000U, 0, 4, 3}),
        std::overflow_error);
    static_cast<void>(fixture.Call("libGLESv2.so", "glClearColor",
                 {std::bit_cast<std::uint32_t>(0.125F),
                  std::bit_cast<std::uint32_t>(0.25F),
                  std::bit_cast<std::uint32_t>(0.5F),
                  std::bit_cast<std::uint32_t>(1.0F)}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glClear", {0x00004000U}));
    CHECK(fixture.Call("libEGL.so", "eglSwapBuffers", {1, 3}) == 1);
    const auto frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->width == 4);
    CHECK(frame->height == 3);
    CHECK(frame->rgba8.size() == 4U * 3U * 4U);
    CHECK(frame->rgba8[0] == doctest::Approx(32).epsilon(0.02));
    CHECK(frame->rgba8[1] == doctest::Approx(64).epsilon(0.02));
    CHECK(frame->rgba8[2] == doctest::Approx(128).epsilon(0.02));
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1}) == 1);
}

TEST_CASE("opaque video readback filter preserves GL drawing and resumes fresh base frames") {
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    fixture.boundary.SetFrameReadbackFilter({nullptr, +[](void*) { return true; }});
    CHECK(fixture.Call("libGLESv2.so", "glClearColor",
        {std::bit_cast<std::uint32_t>(1.0F), 0U, 0U, std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glClear", {0x00004000U}) == 0U);
    fixture.boundary.PresentManagedSurface();
    CHECK_FALSE(fixture.boundary.TakeLatestFrame().has_value());
    CHECK(fixture.boundary.Stats().clears == 1U);
    fixture.boundary.SetFrameReadbackFilter({nullptr, +[](void*) { return false; }});
    fixture.boundary.PresentManagedSurface();
    const auto frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->rgba8[0] == 255U);
    CHECK(frame->rgba8[1] == 0U);
    CHECK(frame->sequence == 1U);
    fixture.boundary.SetFrameReadbackFilter({});
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("async window presentation warms once drains final frame and retires old GPU jobs") {
    BoundaryFixture fixture(1, nullptr, {}, true, {}, true);
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);
    REQUIRE(fixture.Call("libEGL.so", "eglCreateWindowSurface", {1U,2U,1U,0U}) == 3U);
    fixture.bus.Write32(fixture.output, 0x3098U, 1U);
    fixture.bus.Write32(fixture.output.Add(4U), 2U, 1U);
    fixture.bus.Write32(fixture.output.Add(8U), 0x3038U, 1U);
    REQUIRE(fixture.Call("libEGL.so", "eglCreateContext", {1U,2U,0U,fixture.output.Value()}) == 4U);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1,3,3,4}) == 1U);
    const auto clear = [&](float r, float g, float b) {
        CHECK(fixture.Call("libGLESv2.so", "glClearColor", {std::bit_cast<std::uint32_t>(r),
            std::bit_cast<std::uint32_t>(g), std::bit_cast<std::uint32_t>(b), std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
        CHECK(fixture.Call("libGLESv2.so", "glClear", {0x4000U}) == 0U);
    };
    clear(1,0,0);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U, 3U}) == 1U);
    const auto first = fixture.boundary.TakeLatestFrame();
    REQUIRE(first.has_value());
    CHECK(first->sequence == 1U);
    CHECK(first->rgba8[0] == 255U);
    CHECK_FALSE(fixture.boundary.TakeLatestFrame().has_value());
    clear(0,1,0);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U, 3U}) == 1U);
    fixture.bus.Write32(fixture.stack, 0x1908U, 1U);
    fixture.bus.Write32(fixture.stack.Add(4U), 0x1401U, 1U);
    fixture.bus.Write32(fixture.stack.Add(8U), fixture.output.Value(), 1U);
    REQUIRE(fixture.Call("libGLESv2.so", "glReadPixels", {0,0,1,1}) == 0U);
    CHECK(fixture.bus.Read8(fixture.output, 1U) == 0U);
    CHECK(fixture.bus.Read8(fixture.output.Add(1U), 1U) == 255U);
    CHECK(fixture.bus.Read8(fixture.output.Add(2U), 1U) == 0U);
    std::optional<ogplay::runtime::AndroidBoundaryFrame> last;
    const auto deadline = ogplay::hal::Clock::SteadyTimestampNs() + 3'000'000'000ULL;
    // No next swap: an idle/WHEN_DIRTY producer must still deliver its last frame.
    while (!last && ogplay::hal::Clock::SteadyTimestampNs() < deadline) {
        last = fixture.boundary.TakeLatestFrame();
        std::this_thread::yield();
    }
    REQUIRE(last.has_value());
    CHECK(last->sequence == 2U);
    CHECK(last->rgba8[0] == 0U);
    CHECK(last->rgba8[1] == 255U);
    clear(0,0,1);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U, 3U}) == 1U);
    fixture.boundary.PublishSoftwareFrame(std::vector<std::uint8_t>(4U*3U*4U, 77));
    const auto software = fixture.boundary.TakeLatestFrame();
    REQUIRE(software.has_value());
    CHECK(software->rgba8 == std::vector<std::uint8_t>(48U,77U));
    CHECK_FALSE(fixture.boundary.TakeLatestFrame().has_value());
    clear(1,0,0);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U, 3U}) == 1U);
    const auto restarted = fixture.boundary.TakeLatestFrame();
    REQUIRE(restarted.has_value());
    CHECK(restarted->rgba8[0] == 255U);
    clear(0,0,1);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U,3U}) == 1U);
    fixture.bus.Write32(fixture.output, 0x3098U, 1U);
    fixture.bus.Write32(fixture.output.Add(4U), 2U, 1U);
    fixture.bus.Write32(fixture.output.Add(8U), 0x3038U, 1U);
    const auto other_context = fixture.Call("libEGL.so", "eglCreateContext", {1U,2U,4U,fixture.output.Value()});
    REQUIRE(other_context != 0U);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1U,3U,3U,other_context}) == 1U);
    clear(0,1,0);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U,3U}) == 1U);
    const auto switched = fixture.boundary.TakeLatestFrame();
    REQUIRE(switched.has_value());
    CHECK(switched->rgba8[0] == 0U);
    CHECK(switched->rgba8[1] == 255U);
    CHECK_FALSE(fixture.boundary.TakeLatestFrame().has_value());
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1,0,0,0}) == 1U);
}
