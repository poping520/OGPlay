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
#include "ogplay/video/video_player.h"
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

TEST_CASE("composed frame recycling survives display rotation and late returns") {
    BoundaryFixture fixture;
    auto pixels = std::make_shared<ogplay::video::VideoFrame>();
    pixels->width = 3U;
    pixels->height = 4U;
    pixels->rgba8.assign(48U, 17U);
    fixture.boundary.PublishSharedFrame(pixels);
    REQUIRE(fixture.boundary.TakeLatestFrame());
    fixture.boundary.PublishBlankFrame(255U);
    auto portrait = fixture.boundary.TakeLatestFrame();
    REQUIRE(portrait);
    CHECK(portrait->width == 3U);
    CHECK(portrait->height == 4U);
    const auto* storage = portrait->rgba8.data();
    // A prior orientation can be returned after a new frame has been published.
    pixels = std::make_shared<ogplay::video::VideoFrame>();
    pixels->width = 4U;
    pixels->height = 3U;
    pixels->rgba8.assign(48U, 31U);
    fixture.boundary.PublishSharedFrame(pixels);
    CHECK_NOTHROW(fixture.boundary.RecycleFrame(std::move(*portrait)));
    fixture.boundary.PublishBlankFrame(0U);
    auto landscape = fixture.boundary.TakeLatestFrame();
    REQUIRE(landscape);
    CHECK(landscape->width == 4U);
    CHECK(landscape->height == 3U);
    CHECK(landscape->rgba8.data() == storage);
    CHECK(landscape->rgba8 == std::vector<std::uint8_t>(48U, 0U));
    landscape->rgba8.pop_back();
    CHECK_THROWS_AS(fixture.boundary.RecycleFrame(std::move(*landscape)), std::invalid_argument);
}

TEST_CASE("Android boundary supersamples without changing guest surface size") {
    CHECK_THROWS_AS(BoundaryFixture(0), std::invalid_argument);
    CHECK_THROWS_AS(BoundaryFixture(5), std::invalid_argument);
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;

    BoundaryFixture fixture(2);
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);
    CHECK(fixture.Call("libEGL.so", "eglQuerySurface", {1, 3, 0x3057U, fixture.output.Value()}) == 1);
    CHECK(fixture.bus.Read32(fixture.output) == 4);
    CHECK(fixture.Call("libEGL.so", "eglQuerySurface", {1, 3, 0x3056U, fixture.output.Value()}) == 1);
    CHECK(fixture.bus.Read32(fixture.output) == 3);

    const auto targets = fixture.boundary.RenderTargets();
    REQUIRE(targets.size() == 1);
    CHECK(targets[0].width == 8);
    CHECK(targets[0].height == 6);

    static_cast<void>(fixture.Call("libGLESv2.so", "glViewport", {0, 0, 4, 3}));
    CHECK_THROWS_AS(fixture.Call("libGLESv2.so", "glViewport", {0x40000000U, 0, 4, 3}), std::overflow_error);
    static_cast<void>(fixture.Call("libGLESv2.so", "glClearColor",
                                   {std::bit_cast<std::uint32_t>(0.125F), std::bit_cast<std::uint32_t>(0.25F),
                                    std::bit_cast<std::uint32_t>(0.5F), std::bit_cast<std::uint32_t>(1.0F)}));
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
    REQUIRE(fixture.Call("libEGL.so", "eglCreateWindowSurface", {1U, 2U, 1U, 0U}) == 3U);
    fixture.bus.Write32(fixture.output, 0x3098U, 1U);
    fixture.bus.Write32(fixture.output.Add(4U), 2U, 1U);
    fixture.bus.Write32(fixture.output.Add(8U), 0x3038U, 1U);
    REQUIRE(fixture.Call("libEGL.so", "eglCreateContext", {1U, 2U, 0U, fixture.output.Value()}) == 4U);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1U);
    const auto clear = [&](float r, float g, float b) {
        CHECK(fixture.Call("libGLESv2.so", "glClearColor",
                           {std::bit_cast<std::uint32_t>(r), std::bit_cast<std::uint32_t>(g),
                            std::bit_cast<std::uint32_t>(b), std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
        CHECK(fixture.Call("libGLESv2.so", "glClear", {0x4000U}) == 0U);
    };
    clear(1, 0, 0);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U, 3U}) == 1U);
    const auto first = fixture.boundary.TakeLatestFrame();
    REQUIRE(first.has_value());
    CHECK(first->sequence == 1U);
    CHECK(first->rgba8[0] == 255U);
    CHECK_FALSE(fixture.boundary.TakeLatestFrame().has_value());
    clear(0, 1, 0);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U, 3U}) == 1U);
    fixture.bus.Write32(fixture.stack, 0x1908U, 1U);
    fixture.bus.Write32(fixture.stack.Add(4U), 0x1401U, 1U);
    fixture.bus.Write32(fixture.stack.Add(8U), fixture.output.Value(), 1U);
    REQUIRE(fixture.Call("libGLESv2.so", "glReadPixels", {0, 0, 1, 1}) == 0U);
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
    clear(0, 0, 1);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U, 3U}) == 1U);
    fixture.boundary.PublishSoftwareFrame(std::vector<std::uint8_t>(4U * 3U * 4U, 77));
    auto software = fixture.boundary.TakeLatestFrame();
    REQUIRE(software.has_value());
    CHECK(software->rgba8 == std::vector<std::uint8_t>(48U, 77U));
    CHECK_FALSE(fixture.boundary.TakeLatestFrame().has_value());

    const auto* storage = software->rgba8.data();
    const auto software_sequence = software->sequence;
    fixture.boundary.RecycleFrame(std::move(*software));
    fixture.boundary.PublishBlankFrame(0U);
    fixture.boundary.PublishBlankFrame(255U);
    const auto blank = fixture.boundary.TakeLatestFrame();
    REQUIRE(blank);
    CHECK(blank->sequence == software_sequence + 2U);
    CHECK(blank->rgba8.data() == storage);
    for (std::size_t i = 0; i < blank->rgba8.size(); ++i)
        CHECK(blank->rgba8[i] == (i % 4U == 3U ? 255U : 0U));
    CHECK_FALSE(fixture.boundary.TakeLatestFrame().has_value());
    clear(1, 0, 0);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U, 3U}) == 1U);
    const auto restarted = fixture.boundary.TakeLatestFrame();
    REQUIRE(restarted.has_value());
    CHECK(restarted->rgba8[0] == 255U);
    clear(0, 0, 1);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U, 3U}) == 1U);
    fixture.boundary.PublishBlankFrame(0U);
    const auto transparent = fixture.boundary.TakeLatestFrame();
    REQUIRE(transparent);
    CHECK(transparent->rgba8 == std::vector<std::uint8_t>(48U, 0U));
    CHECK_FALSE(fixture.boundary.TakeLatestFrame().has_value());

    // Warm a new collector and leave work pending before a context switch.
    clear(1, 0, 0);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U, 3U}) == 1U);
    REQUIRE(fixture.boundary.TakeLatestFrame());
    clear(0, 0, 1);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U, 3U}) == 1U);
    fixture.bus.Write32(fixture.output, 0x3098U, 1U);
    fixture.bus.Write32(fixture.output.Add(4U), 2U, 1U);
    fixture.bus.Write32(fixture.output.Add(8U), 0x3038U, 1U);
    const auto other_context = fixture.Call("libEGL.so", "eglCreateContext", {1U, 2U, 4U, fixture.output.Value()});
    REQUIRE(other_context != 0U);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1U, 3U, 3U, other_context}) == 1U);
    clear(0, 1, 0);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1U, 3U}) == 1U);
    const auto switched = fixture.boundary.TakeLatestFrame();
    REQUIRE(switched.has_value());
    CHECK(switched->rgba8[0] == 0U);
    CHECK(switched->rgba8[1] == 255U);
    CHECK_FALSE(fixture.boundary.TakeLatestFrame().has_value());
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 0, 0, 0}) == 1U);
}

TEST_CASE("frame-rate cap holds guest window swaps to the period but not managed swaps") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    constexpr std::uint64_t kPeriodNs = 40'000'000U;
    BoundaryFixture fixture(1, nullptr, {}, true, {}, false, ogplay::gles::CompressedTextureFamily::all, kPeriodNs);
    PrepareNativeEgl(fixture);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1U);
    const auto now = [] { return ogplay::hal::Clock::SteadyTimestampNs(); };
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1, 3}) == 1U);
    const auto first = now();
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1, 3}) == 1U);
    REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1, 3}) == 1U);
    // Swaps from guest code are released one period apart, each after its frame is published.
    CHECK(now() - first >= 2U * kPeriodNs - 2'000'000U);
    CHECK(fixture.boundary.TakeLatestFrame().has_value());
    // Managed facades swap once per paced lifecycle frame and are not held a second time.
    const auto managed = now();
    for (int swap = 0; swap < 3; ++swap)
        REQUIRE(fixture.boundary.InvokeManagedEgl("eglSwapBuffers", std::array{1U, 3U}, 1U) == 1U);
    CHECK(now() - managed < kPeriodNs);
}

TEST_CASE("guest window swaps offer pacing to the session before the frame-rate cap") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    constexpr std::uint64_t kPeriodNs = 40'000'000U;
    BoundaryFixture fixture(1, nullptr, {}, true, {}, false, ogplay::gles::CompressedTextureFamily::all, kPeriodNs);

    struct Pacing {
        BoundaryFixture& fixture;
        std::vector<std::uint32_t> surfaces;
        std::vector<std::future<void>> others{};
        bool handled{true};
        bool unlocked{true};
    } pacing{fixture, {}};

    fixture.boundary.SetSurfaceHooks(
        {.owner = &pacing, .pace_swap = +[](void* owner, const std::uint32_t surface) {
             auto& state = *static_cast<Pacing*>(owner);
             state.surfaces.push_back(surface);
             // Another thread must be able to use EGL and the window while this swap waits.
             state.others.push_back(std::async(std::launch::async, [&state] {
                 static_cast<void>(
                     state.fixture.boundary.InvokeManagedEgl("eglDestroyContext", std::array{0U, 0U}, 2U));
                 state.fixture.boundary.UpdateSurfaceWindow({ogplay::memory::GuestAddress{0x1234}, 2, 2});
             }));
             state.unlocked =
                 state.unlocked && state.others.back().wait_for(std::chrono::seconds(2)) == std::future_status::ready;
             return state.handled;
         }});
    PrepareNativeEgl(fixture);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1U);
    const auto now = [] { return ogplay::hal::Clock::SteadyTimestampNs(); };
    const auto swap = [&](unsigned count) {
        const auto begin = now();
        for (unsigned i = 0; i < count; ++i)
            REQUIRE(fixture.Call("libEGL.so", "eglSwapBuffers", {1, 3}) == 1U);
        return now() - begin;
    };
    swap(4);
    CHECK(pacing.surfaces == std::vector<std::uint32_t>(4, 3U));
    CHECK(pacing.unlocked);
    // A handled swap is not held a second time, so the cap has not started counting yet.
    pacing.handled = false;
    swap(1);
    CHECK(swap(2) >= 2U * kPeriodNs - 2'000'000U);
    CHECK(pacing.surfaces.size() == 7U);
    // Managed facades pace themselves, and a failed swap publishes no frame to pace.
    REQUIRE(fixture.boundary.InvokeManagedEgl("eglSwapBuffers", std::array{1U, 3U}, 1U) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglSwapBuffers", {1, 99}) == 0U);
    const auto pbuffer = fixture.Call("libEGL.so", "eglCreatePbufferSurface", {1, 2, 0});
    if (pbuffer != 0U && fixture.Call("libEGL.so", "eglMakeCurrent", {1, pbuffer, pbuffer, 4}) == 1U)
        CHECK(fixture.Call("libEGL.so", "eglSwapBuffers", {1, pbuffer}) == 1U);
    CHECK(pacing.surfaces.size() == 7U);
    fixture.boundary.SetSurfaceHooks({});
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 0, 0, 0}) == 1U);
}

TEST_CASE("API19 NDK window CPU lock publishes packed pixels and excludes EGL") {
    using namespace ogplay;
    BoundaryFixture f;
    const memory::GuestAddress identity{0x1234};
    std::shared_ptr<video::VideoFrame> frame;
    f.boundary.SetSurfaceHooks({
        &frame,
        +[](void*, std::uint64_t, memory::GuestAddress, runtime::SurfaceJavaReference) {
            return memory::GuestAddress{};
        },
        +[](void* owner, memory::GuestAddress target, std::shared_ptr<video::VideoFrame> pixels) {
            CHECK(target == memory::GuestAddress{0x1234});
            *static_cast<std::shared_ptr<video::VideoFrame>*>(owner) = std::move(pixels);
        },
    });
    f.boundary.UpdateSurfaceWindow({identity, 2, 1});
    const auto window = f.boundary.AcquireSurfaceWindow({identity, 2, 1}).Value();
    const auto dirty = f.output.Add(44);
    const auto lock = [&] {
        return f.Call("libandroid.so", "ANativeWindow_lock", {window, f.output.Value(), dirty.Value()});
    };
    const auto post = [&] { return f.Call("libandroid.so", "ANativeWindow_unlockAndPost", {window}); };
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1);
    const auto surface = f.Call("libEGL.so", "eglCreateWindowSurface", {1, 2, identity.Value(), 0});
    REQUIRE(surface);
    CHECK(lock() == static_cast<std::uint32_t>(-22));
    REQUIRE(f.Call("libEGL.so", "eglDestroySurface", {1, surface}) == 1);
    CHECK(post() == static_cast<std::uint32_t>(-38));
    CHECK_THROWS(f.Call("libandroid.so", "ANativeWindow_lock", {window, 0, dirty.Value()}));
    CHECK_THROWS(f.Call("libandroid.so", "ANativeWindow_lock", {window, f.output.Value(), 1}));
    REQUIRE(lock() == 0);
    CHECK(lock() == static_cast<std::uint32_t>(-38));
    CHECK(f.bus.Read32(f.output) == 2);
    CHECK(f.bus.Read32(f.output.Add(4)) == 1);
    CHECK(f.bus.Read32(f.output.Add(8)) == 2);
    CHECK(f.bus.Read32(f.output.Add(12)) == 1);
    for (std::uint64_t i = 5; i < 11; ++i)
        CHECK(f.bus.Read32(f.output.Add(i * 4)) == 0);
    CHECK(f.bus.Read32(dirty) == 0);
    CHECK(f.bus.Read32(dirty.Add(4)) == 0);
    CHECK(f.bus.Read32(dirty.Add(8)) == 2);
    CHECK(f.bus.Read32(dirty.Add(12)) == 1);
    const memory::GuestAddress rgba{f.bus.Read32(f.output.Add(16))};
    constexpr std::array<std::uint8_t, 8> rgba_bytes{1, 2, 3, 4, 5, 6, 7, 8};
    f.memory.Write(rgba, std::as_bytes(std::span(rgba_bytes)));
    REQUIRE(post() == 0);
    REQUIRE(frame);
    CHECK(frame->width == 2);
    CHECK(frame->height == 1);
    CHECK(frame->rgba8 == std::vector<std::uint8_t>(rgba_bytes.begin(), rgba_bytes.end()));
    REQUIRE(lock() == 0);
    CHECK(f.bus.Read32(f.output.Add(16)) == rgba.Value());
    REQUIRE(post() == 0);
    // CPU ownership persists after posting, and applies to aliases of the holder.
    CHECK(f.Call("libEGL.so", "eglCreateWindowSurface", {1, 2, identity.Value(), 0}) == 0);
    CHECK(f.Call("libEGL.so", "eglGetError") == 0x3003);
    CHECK(f.Call("libandroid.so", "ANativeWindow_setBuffersGeometry", {window, 0, 0, 3}) ==
          static_cast<std::uint32_t>(-22));
    REQUIRE(f.Call("libandroid.so", "ANativeWindow_setBuffersGeometry", {window, 2, 1, 2}) == 0);
    REQUIRE(lock() == 0);
    CHECK(f.bus.Read32(f.output.Add(12)) == 2);
    f.memory.Write(memory::GuestAddress{f.bus.Read32(f.output.Add(16))}, std::as_bytes(std::span(rgba_bytes)));
    REQUIRE(post() == 0);
    CHECK(frame->rgba8 == std::vector<std::uint8_t>{1, 2, 3, 255, 5, 6, 7, 255});
    REQUIRE(f.Call("libandroid.so", "ANativeWindow_setBuffersGeometry", {window, 2, 1, 4}) == 0);
    REQUIRE(lock() == 0);
    CHECK(f.bus.Read32(f.output.Add(12)) == 4);
    const memory::GuestAddress rgb565{f.bus.Read32(f.output.Add(16))};
    f.memory.Write(rgb565, std::array{std::byte{0}, std::byte{0xf8}, std::byte{0xff}, std::byte{0x07}});
    REQUIRE(post() == 0);
    CHECK(frame->rgba8 == std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 255, 255});
    REQUIRE(lock() == 0);
    f.boundary.RetireSurfaceWindow(identity);
    CHECK_THROWS(static_cast<void>(f.bus.Read32(rgb565)));
    CHECK(post() == static_cast<std::uint32_t>(-38));
    f.Call("libandroid.so", "ANativeWindow_release", {window});
    CHECK_THROWS(post());
}
