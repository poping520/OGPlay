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

TEST_CASE("BND45 Android looper polls real pipe readiness and attached input") {
    ogplay::runtime::VirtualFileSystem fs;
    BoundaryFixture fixture(1, nullptr, {}, true, {[&](std::int32_t fd) { return fs.PipePollEvents(fd); }, {}});
    const auto pipe = fs.CreatePipe();
    const auto looper = fixture.Call("libandroid.so", "ALooper_prepare", {1});
    fixture.bus.Write32(fixture.stack, 0);
    fixture.bus.Write32(fixture.stack.Add(4), 0x12345678U);
    CHECK(fixture.Call("libandroid.so", "ALooper_addFd", {looper, static_cast<std::uint32_t>(pipe.read_descriptor), 1, 1}) == 1);
    fixture.boundary.NotifyFileWrite();
    CHECK(static_cast<std::int32_t>(fixture.Call("libandroid.so", "ALooper_pollAll")) == -3);
    const std::array payload{std::byte{42}};
    CHECK(fs.Write(pipe.write_descriptor, payload) == 1);
    CHECK(fixture.Call("libandroid.so", "ALooper_pollOnce",
                       {0, fixture.output.Value(), fixture.output.Add(4).Value(),
                        fixture.output.Add(8).Value()}) == 1);
    CHECK(fixture.bus.Read32(fixture.output) == static_cast<std::uint32_t>(pipe.read_descriptor));
    CHECK(fixture.bus.Read32(fixture.output.Add(8)) == 0x12345678U);
    std::array<std::byte, 1> read{};
    CHECK(fs.Read(pipe.read_descriptor, read) == 1);
    CHECK(static_cast<std::int32_t>(fixture.Call("libandroid.so", "ALooper_pollAll")) == -3);
    CHECK(fixture.Call("libandroid.so", "ALooper_removeFd", {looper, static_cast<std::uint32_t>(pipe.read_descriptor)}) == 1);
    CHECK(fixture.Call("libandroid.so", "ALooper_removeFd", {looper, static_cast<std::uint32_t>(pipe.read_descriptor)}) == 0);
    fixture.bus.Write32(fixture.stack, 0x87654321U);
    fixture.Call("libandroid.so", "AInputQueue_attachLooper", {2, looper, 2, 0});
    fixture.boundary.PushInput({ogplay::runtime::AndroidBoundaryInputType::key, 29, 0, 0, true});
    CHECK(fixture.Call("libandroid.so", "ALooper_pollAll", {0, 0, 0, fixture.output.Add(8).Value()}) == 2);
    CHECK(fixture.bus.Read32(fixture.output.Add(8)) == 0x87654321U);
    CHECK(fixture.Call("libandroid.so", "AInputQueue_getEvent", {2, fixture.output.Value()}) == 0);
    fixture.Call("libandroid.so", "AInputQueue_finishEvent", {2, fixture.bus.Read32(fixture.output), 1});
    fixture.Call("libandroid.so", "AInputQueue_detachLooper", {2});
    fixture.boundary.PushInput({ogplay::runtime::AndroidBoundaryInputType::key, 30, 0, 0, true});
    CHECK(static_cast<std::int32_t>(fixture.Call("libandroid.so", "ALooper_pollOnce")) == -3);
}

TEST_CASE("BND45 Android looper validates registrations replaces data and isolates ready sources") {
    using namespace ogplay;
    runtime::VirtualFileSystem fs;
    BoundaryFixture f(1, nullptr, {}, true, {[&](std::int32_t fd) { return fs.PipePollEvents(fd); }, {}});
    const auto pipe = fs.CreatePipe();
    const auto fd = static_cast<std::uint32_t>(pipe.read_descriptor);
    const auto looper = f.Call("libandroid.so", "ALooper_prepare", {1});
    f.bus.Write32(f.stack, 0);
    f.bus.Write32(f.stack.Add(4), 11);
    CHECK(static_cast<std::int32_t>(f.Call("libandroid.so", "ALooper_addFd", {looper, 9999, 1, 1})) == -1);
    fs.PutFile("/plain", {}, true);
    const auto plain = fs.Open("/plain", {.read = true});
    CHECK_FALSE(fs.PipePollEvents(plain).has_value());
    CHECK(static_cast<std::int32_t>(f.Call("libandroid.so", "ALooper_addFd", {looper, static_cast<std::uint32_t>(plain), 1, 1})) == -1);
    f.bus.Write32(f.stack, 0x10000);
    CHECK(static_cast<std::int32_t>(f.Call("libandroid.so", "ALooper_addFd", {looper, fd, 1, 1})) == -1);
    f.bus.Write32(f.stack, 0);
    CHECK(f.Call("libandroid.so", "ALooper_addFd", {looper, fd, 1, 1}) == 1);
    f.bus.Write32(f.stack.Add(4), 22);
    CHECK(f.Call("libandroid.so", "ALooper_addFd", {looper, fd, 7, 1}) == 1);
    const std::array bytes{std::byte{1}};
    CHECK(fs.Write(pipe.write_descriptor, bytes) == 1);
    FastBoundaryCall(f, "libandroid.so", "ALooper_prepare", {1}, 20);
    CHECK(static_cast<std::int32_t>(FastBoundaryCall(f, "libandroid.so", "ALooper_pollOnce", {}, 20)) == -3);
    f.bus.Write32(f.output, 0x1234);
    CHECK_THROWS(f.Call("libandroid.so", "ALooper_pollOnce", {0, f.output.Value(), 0x10, 0}));
    CHECK(f.bus.Read32(f.output) == 0x1234);
    CHECK(f.Call("libandroid.so", "ALooper_pollOnce", {0, 0, 0, f.output.Value()}) == 7);
    CHECK(f.bus.Read32(f.output) == 22);
    std::array<std::byte, 1> buffer{};
    CHECK(fs.Read(pipe.read_descriptor, buffer) == 1);
    CHECK(static_cast<std::int32_t>(f.Call("libandroid.so", "ALooper_pollOnce", {0, 0, 0, f.output.Value()})) == -3);
    CHECK(f.bus.Read32(f.output) == 0);
}

TEST_CASE("BND45 Android looper identity references and thread isolation") {
    BoundaryFixture f;
    const auto call = [&](std::uint64_t tid, std::string_view name, std::array<std::uint32_t, 4> args = {}) {
        return FastBoundaryCall(f, "libandroid.so", name, args, tid);
    };
    CHECK(call(10, "ALooper_forThread") == 0);
    CHECK(static_cast<std::int32_t>(call(10, "ALooper_pollOnce")) == -4);
    const auto first = call(10, "ALooper_prepare", {1});
    REQUIRE(first != 0);
    CHECK(call(10, "ALooper_prepare") == first);
    CHECK(call(10, "ALooper_forThread") == first);
    CHECK(call(11, "ALooper_forThread") == 0);
    const auto second = call(11, "ALooper_prepare");
    CHECK(second != first);
    CHECK(call(11, "ALooper_acquire", {first}) == 0);
    f.boundary.RetireThreadLooper(10);
    CHECK(call(10, "ALooper_forThread") == 0);
    CHECK(call(11, "ALooper_wake", {first}) == 0);
    CHECK(call(11, "ALooper_release", {first}) == 0);
    const auto third = call(10, "ALooper_prepare");
    CHECK(third != first);
    CHECK(call(11, "ALooper_forThread") == second);
    // Slow route exposes structured errors from an invalid/stale identity.
    CHECK_THROWS(f.Call("libandroid.so", "ALooper_wake", {first}));
    CHECK_THROWS(f.Call("libandroid.so", "ALooper_release", {third}));
}

TEST_CASE("BND45 Android looper wake timeout shutdown and owner exit unblock waits") {
    std::atomic_bool running{true};
    BoundaryFixture f(1, nullptr, {}, true, {{}, [&](std::uint64_t) { return running.load(); }});
    const auto looper = FastBoundaryCall(f, "libandroid.so", "ALooper_prepare", {}, 10);
    CHECK(static_cast<std::int32_t>(FastBoundaryCall(f, "libandroid.so", "ALooper_pollOnce", {1}, 10)) == -3);
    auto waiting = std::async(std::launch::async, [&] {
        return static_cast<std::int32_t>(FastBoundaryCall(f, "libandroid.so", "ALooper_pollOnce", {0xffffffffU}, 10));
    });
    CHECK(FastBoundaryCall(f, "libandroid.so", "ALooper_wake", {looper}, 11) == 0);
    CHECK(waiting.get() == -1);
    auto cancelled = std::async(std::launch::async, [&] {
        return static_cast<std::int32_t>(FastBoundaryCall(f, "libandroid.so", "ALooper_pollAll", {0xffffffffU}, 10));
    });
    SUBCASE("owner exits") { running = false; }
    SUBCASE("process shuts down") { f.boundary.ShutdownLoopers(); }
    CHECK(cancelled.get() == -4);
    CHECK_THROWS_WITH(f.Call("libandroid.so", "ALooper_prepare"),
                      "ALooper_prepare on a retired thread");
}

TEST_CASE("BND45 pipe readiness reflects data EOF peers and unsupported descriptors") {
    ogplay::runtime::VirtualFileSystem fs;
    const auto pipe = fs.CreatePipe();
    REQUIRE(fs.PipePollEvents(pipe.read_descriptor).has_value());
    CHECK(*fs.PipePollEvents(pipe.read_descriptor) == 0);
    CHECK(*fs.PipePollEvents(pipe.write_descriptor) == 2);
    const std::array data{std::byte{7}};
    CHECK(fs.Write(pipe.write_descriptor, data) == 1);
    CHECK(*fs.PipePollEvents(pipe.read_descriptor) == 1);
    SUBCASE("writer closes") {
        fs.Close(pipe.write_descriptor);
        CHECK(*fs.PipePollEvents(pipe.read_descriptor) == 9);
        CHECK(*fs.PipePollEvents(pipe.write_descriptor) == 16);
    }
    SUBCASE("reader closes") {
        fs.Close(pipe.read_descriptor);
        CHECK(*fs.PipePollEvents(pipe.write_descriptor) == 4);
    }
}
