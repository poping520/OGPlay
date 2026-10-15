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

namespace {

class BoundaryAssetSource final : public ogplay::runtime::AndroidAssetSource {
public:
    std::uint64_t length{3};
    bool compressed{};
    mutable std::size_t largest_read{}, descriptor_opens{}, descriptor_closes{};
    std::uint64_t Length() const override { return length; }
    std::size_t ReadAt(std::uint64_t offset, std::span<std::byte> output) const override {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), length - offset));
        largest_read = std::max(largest_read, count);
        const std::array payload{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
        for (std::size_t i = 0; i < count; ++i) output[i] = payload[(offset + i) % payload.size()];
        return count;
    }
    ogplay::runtime::AssetFileDescriptor OpenFileDescriptor() const override {
        if (compressed) return {};
        ++descriptor_opens;
        return {42, 123, length};
    }
    void CloseFileDescriptor(std::int32_t) const override { ++descriptor_closes; }
};
class BoundaryAssetManager final : public ogplay::runtime::AndroidAssetManagerSource {
public:
    std::shared_ptr<BoundaryAssetSource> source = std::make_shared<BoundaryAssetSource>();
    std::shared_ptr<const ogplay::runtime::AndroidAssetSource> Open(std::string_view name) override {
        return name == "hello" ? source : nullptr;
    }
};


} // namespace

TEST_CASE("DVM-195 registered NativeActivity assets window and input are bounded and retired") {
    using namespace ogplay;
    BoundaryFixture f;
    const auto activity = f.output.Add(256), queue = activity.Add(148);
    const auto assets = f.boundary.RegisterAssetManager(std::make_shared<BoundaryAssetManager>());
    f.boundary.RegisterNativeActivity({activity, assets, queue, 4, 3});
    WriteGuestString(f, f.output, "hello");
    const auto asset = f.Call("libandroid.so", "AAssetManager_open", {assets.Value(), f.output.Value(), 2});
    REQUIRE(asset != 0);
    CHECK(f.Call("libandroid.so", "AAsset_getLength", {asset}) == 3);
    CHECK(f.Call("libandroid.so", "AAsset_read", {asset, f.output.Add(32).Value(), 2}) == 2);
    CHECK(f.Call("libandroid.so", "AAsset_getRemainingLength", {asset}) == 1);
    CHECK(f.Call("libandroid.so", "AAsset_seek", {asset, 0, 0}) == 0);
    CHECK(f.Call("libandroid.so", "AAsset_read", {asset, f.output.Add(32).Value(), 8}) == 3);
    std::array<std::byte, 3> actual{}; f.memory.Read(f.output.Add(32), actual);
    CHECK(actual == std::array{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}});
    CHECK(f.Call("libandroid.so", "AAsset_read", {asset, f.output.Add(32).Value(), 8}) == 0);
    f.Call("libandroid.so", "AAsset_close", {asset});
    CHECK_THROWS(f.Call("libandroid.so", "AAsset_getLength", {asset}));
    const auto window = f.boundary.SetNativeActivityWindow(activity, true);
    CHECK(f.Call("libandroid.so", "ANativeWindow_getWidth", {window.Value()}) == 4);
    CHECK(f.Call("libandroid.so", "ANativeWindow_getHeight", {window.Value()}) == 3);
    CHECK(f.Call("libandroid.so", "ANativeWindow_setBuffersGeometry", {window.Value(), 0, 0, 1}) == 0);
    CHECK(static_cast<std::int32_t>(f.Call("libandroid.so", "ANativeWindow_setBuffersGeometry", {window.Value(), 8, 3, 1})) == -22);
    f.boundary.SetNativeActivityInput(activity, true);
    f.bus.Write32(f.stack, 0x1234);
    CHECK_THROWS(f.Call("libandroid.so", "AInputQueue_attachLooper", {queue.Value(), 1, 2, 1}));
    const auto looper = f.Call("libandroid.so", "ALooper_prepare", {1});
    f.Call("libandroid.so", "AInputQueue_attachLooper", {queue.Value(), looper, 2, 0});
    f.boundary.PushInput({runtime::AndroidBoundaryInputType::key, 29, 0, 0, true});
    CHECK(f.Call("libandroid.so", "ALooper_pollAll", {0, 0, 0, f.output.Value()}) == 2);
    CHECK(f.bus.Read32(f.output) == 0x1234);
    CHECK(f.Call("libandroid.so", "AInputQueue_getEvent", {queue.Value(), f.output.Value()}) == 0);
    const auto event = f.bus.Read32(f.output);
    CHECK(f.Call("libandroid.so", "AKeyEvent_getKeyCode", {event}) == 29);
    f.Call("libandroid.so", "AInputQueue_finishEvent", {queue.Value(), event, 1});
    CHECK_THROWS(f.Call("libandroid.so", "AKeyEvent_getKeyCode", {event}));
    f.Call("libandroid.so", "AInputQueue_detachLooper", {queue.Value()});
    f.boundary.PushInput({runtime::AndroidBoundaryInputType::key, 30, 0, 0, true});
    CHECK(static_cast<std::int32_t>(f.Call("libandroid.so", "ALooper_pollAll")) == -3);
    f.Call("libandroid.so", "ANativeWindow_acquire", {window.Value()});
    f.boundary.UnregisterNativeActivity(activity);
    CHECK(f.Call("libandroid.so", "ANativeWindow_getWidth", {window.Value()}) == 4);
    f.Call("libandroid.so", "ANativeWindow_release", {window.Value()});
    CHECK_THROWS(f.Call("libandroid.so", "ANativeWindow_getWidth", {window.Value()}));
    CHECK_THROWS(f.Call("libandroid.so", "AInputQueue_getEvent", {queue.Value(), f.output.Value()}));
    CHECK_THROWS(f.Call("libandroid.so", "ANativeWindow_setBuffersGeometry", {window.Value(), 0, 0, 0}));
    WriteGuestString(f, f.output, "hello");
    const auto independent_asset = f.Call("libandroid.so", "AAssetManager_open", {assets.Value(), f.output.Value(), 2});
    REQUIRE(independent_asset != 0);
    f.boundary.UnregisterAssetManager(assets);
    CHECK_THROWS(f.Call("libandroid.so", "AAssetManager_open", {assets.Value(), f.output.Value(), 2}));
    CHECK(f.Call("libandroid.so", "AAsset_read", {independent_asset, f.output.Add(32).Value(), 3}) == 3);
    f.Call("libandroid.so", "AAsset_close", {independent_asset});
}

TEST_CASE("DVM-195 NativeActivity window takeover retires old EGL surfaces") {
    using namespace ogplay;
    BoundaryFixture f;
    const auto activity = f.output.Add(256);
    f.boundary.RegisterNativeActivity({activity, activity.Add(128), activity.Add(148), 4, 3});
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1, 0, 0}) == 1U);
    const auto create = [&](memory::GuestAddress window) {
        return f.Call("libEGL.so", "eglCreateWindowSurface", {1, 2, window.Value(), 0});
    };
    CHECK(create(runtime::kNativeActivityWindowHandleBegin) == 0U);
    CHECK(f.Call("libEGL.so", "eglGetError") == 0x300BU);
    const auto first_window = f.boundary.SetNativeActivityWindow(activity, true);
    const auto first_surface = create(first_window);
    REQUIRE(first_surface != 0U);
    f.Call("libandroid.so", "ANativeWindow_acquire", {first_window.Value()});
    std::uint32_t context{};
    if (gles::IsNativeAngleEglAvailable()) {
        context = AuditContext(f);
        AuditBind(f, context, first_surface);
        REQUIRE(f.Call("libEGL.so", "eglSwapBuffers", {1, first_surface}) == 1U);
    }
    f.boundary.SetNativeActivityWindow(activity, false);
    CHECK(create(first_window) == 0U);
    CHECK(f.Call("libEGL.so", "eglGetError") == 0x300BU);
    if (context != 0U) {
        CHECK(f.Call("libEGL.so", "eglSwapBuffers", {1, first_surface}) == 0U);
        CHECK(f.Call("libEGL.so", "eglGetError") == 0x300BU);
    }
    // A retained ANativeWindow stays readable, but a new Surface owns a new
    // identity. Reacquiring ownership must not reactivate the old EGL surface.
    CHECK(f.Call("libandroid.so", "ANativeWindow_getWidth", {first_window.Value()}) == 4U);
    const auto second_window = f.boundary.SetNativeActivityWindow(activity, true);
    CHECK(second_window != first_window);
    const auto second_surface = create(second_window);
    REQUIRE(second_surface != 0U);
    CHECK(create(first_window) == 0U);
    CHECK(f.Call("libEGL.so", "eglGetError") == 0x300BU);
    if (context != 0U) {
        CHECK(f.Call("libEGL.so", "eglMakeCurrent", {1, first_surface, first_surface, context}) == 0U);
        CHECK(f.Call("libEGL.so", "eglGetError") == 0x300BU);
        AuditBind(f, context, second_surface);
        REQUIRE(f.Call("libEGL.so", "eglSwapBuffers", {1, second_surface}) == 1U);
    }
    f.Call("libandroid.so", "ANativeWindow_release", {first_window.Value()});
    CHECK_THROWS(f.Call("libandroid.so", "ANativeWindow_getWidth", {first_window.Value()}));
    f.boundary.UnregisterNativeActivity(activity);
    CHECK(create(second_window) == 0U);
    CHECK(f.Call("libEGL.so", "eglGetError") == 0x300BU);
    if (context != 0U) {
        CHECK(f.Call("libEGL.so", "eglSwapBuffers", {1, second_surface}) == 0U);
        CHECK(f.Call("libEGL.so", "eglGetError") == 0x300BU);
        REQUIRE(f.Call("libEGL.so", "eglMakeCurrent", {1, 0, 0, 0}) == 1U);
        CHECK(f.Call("libEGL.so", "eglDestroyContext", {1, context}) == 1U);
    }
    CHECK(f.Call("libEGL.so", "eglDestroySurface", {1, first_surface}) == 1U);
    CHECK(f.Call("libEGL.so", "eglDestroySurface", {1, second_surface}) == 1U);
    CHECK(f.Call("libEGL.so", "eglTerminate", {1}) == 1U);
}

TEST_CASE("DVM-237 NDK Asset large sources and FD output preflight do not materialize data") {
    using namespace ogplay;
    BoundaryFixture f;
    const auto manager_source = std::make_shared<BoundaryAssetManager>();
    manager_source->source->length = 495689473;
    const auto manager = f.boundary.RegisterAssetManager(manager_source);
    WriteGuestString(f, f.output, "hello");
    CHECK(f.Call("libandroid.so", "AAssetManager_open", {manager.Value(), f.output.Value(), 4}) == 0);
    const auto asset = f.Call("libandroid.so", "AAssetManager_open", {manager.Value(), f.output.Value(), 3});
    REQUIRE(asset != 0);
    CHECK(manager_source->source->largest_read == 0);
    CHECK(f.Call("libandroid.so", "AAsset_getLength", {asset}) == 495689473);
    CHECK(f.Call("libandroid.so", "AAsset_read", {asset, f.output.Add(32).Value(), 16}) == 16);
    CHECK(manager_source->source->largest_read == 16);
    CHECK_THROWS(f.Call("libandroid.so", "AAsset_read", {asset, 1, 16}));
    CHECK(f.Call("libandroid.so", "AAsset_getRemainingLength", {asset}) == 495689473 - 16);
    f.bus.Write32(f.output.Add(64), 0x11223344);
    CHECK_THROWS(f.Call("libandroid.so", "AAsset_openFileDescriptor", {asset, f.output.Add(64).Value(), 1}));
    CHECK(f.bus.Read32(f.output.Add(64)) == 0x11223344);
    CHECK(manager_source->source->descriptor_opens == 0);
    CHECK(f.Call("libandroid.so", "AAsset_openFileDescriptor", {asset, f.output.Add(64).Value(), f.output.Add(68).Value()}) == 42);
    CHECK(f.bus.Read32(f.output.Add(64)) == 123);
    CHECK(f.bus.Read32(f.output.Add(68)) == 495689473);
    manager_source->source->compressed = true;
    f.bus.Write32(f.output.Add(64), 0x11223344);
    CHECK(static_cast<std::int32_t>(f.Call("libandroid.so", "AAsset_openFileDescriptor",
        {asset, f.output.Add(64).Value(), f.output.Add(68).Value()})) == -1);
    CHECK(f.bus.Read32(f.output.Add(64)) == 0x11223344);
    f.boundary.UnregisterAssetManager(manager);
    CHECK(f.Call("libandroid.so", "AAsset_read", {asset, f.output.Add(32).Value(), 3}) == 3);
    f.Call("libandroid.so", "AAsset_close", {asset});
    CHECK_THROWS(f.Call("libandroid.so", "AAsset_getLength", {asset}));
}

TEST_CASE("DVM-237 retiring Asset hooks releases sources and stale handles") {
    using namespace ogplay;
    BoundaryFixture f;
    auto source = std::make_shared<BoundaryAssetManager>();
    const std::weak_ptr<const runtime::AndroidAssetSource> weak = source->source;
    const auto manager = f.boundary.RegisterAssetManager(source);
    WriteGuestString(f, f.output, "hello");
    const auto asset = f.Call("libandroid.so", "AAssetManager_open", {manager.Value(), f.output.Value(), 2});
    REQUIRE(asset != 0);
    source.reset();
    CHECK_FALSE(weak.expired());
    f.boundary.SetAssetManagerHooks({});
    CHECK(weak.expired());
    CHECK_THROWS(f.Call("libandroid.so", "AAsset_getLength", {asset}));
    CHECK_THROWS(f.Call("libandroid.so", "AAssetManager_open", {manager.Value(), f.output.Value(), 2}));
}

TEST_CASE("BND46 input snapshots expose API19 axes history and wide time on both ABI paths") {
    using namespace ogplay;
    BoundaryFixture f;
    runtime::AndroidBoundaryInput input;
    input.type = runtime::AndroidBoundaryInputType::pointer_motion;
    input.action = 5 | (1 << 8);
    input.source = 0x1002;
    input.device_id = 7;
    input.flags = 8; input.meta_state = 3; input.button_state = 2; input.edge_flags = 4;
    input.down_time_ns = 0x123456789LL;
    input.event_time_ns = 0x23456789aLL;
    input.x_offset = 11; input.y_offset = 13;
    input.x_precision = 0.5F; input.y_precision = 0.25F;
    runtime::AndroidInputPointer first, second;
    first.id = 9; second.id = 3; second.tool_type = 2;
    for (std::size_t i = 0; i < 64; ++i) {
        first.axes[i] = static_cast<float>(i) + 0.5F;
        second.axes[i] = static_cast<float>(i) + 10.5F;
    }
    input.pointers = {first, second};
    first.axes[0] = 90; second.axes[0] = 100;
    input.history = {{0x200000001LL, {first, second}}};
    f.boundary.PushInput(input);
    // Mutation after enqueue cannot change the event visible to guest.
    input.pointers[1].axes[0] = -100;
    REQUIRE(f.Call("libandroid.so", "AInputQueue_getEvent", {2, f.output.Value()}) == 0);
    const auto event = f.bus.Read32(f.output);
    const auto scalar = [&](const char* name) { return f.Call("libandroid.so", name, {event}); };
    CHECK(scalar("AInputEvent_getType") == 2);
    CHECK(scalar("AInputEvent_getDeviceId") == 7);
    CHECK(scalar("AInputEvent_getSource") == 0x1002);
    CHECK(scalar("AMotionEvent_getAction") == (5 | 1 << 8));
    CHECK(scalar("AMotionEvent_getFlags") == 8);
    CHECK(scalar("AMotionEvent_getMetaState") == 3);
    CHECK(scalar("AMotionEvent_getButtonState") == 2);
    CHECK(scalar("AMotionEvent_getEdgeFlags") == 4);
    CHECK(scalar("AMotionEvent_getPointerCount") == 2);
    CHECK(scalar("AMotionEvent_getHistorySize") == 1);
    CHECK(std::bit_cast<float>(scalar("AMotionEvent_getXOffset")) == 11);
    CHECK(std::bit_cast<float>(scalar("AMotionEvent_getYOffset")) == 13);
    CHECK(std::bit_cast<float>(scalar("AMotionEvent_getXPrecision")) == 0.5F);
    CHECK(std::bit_cast<float>(scalar("AMotionEvent_getYPrecision")) == 0.25F);
    CHECK(f.Call("libandroid.so", "AMotionEvent_getPointerId", {event, 1}) == 3);
    CHECK(f.Call("libandroid.so", "AMotionEvent_getToolType", {event, 1}) == 2);
    const std::array names{"X", "Y", "Pressure", "Size", "TouchMajor", "TouchMinor",
                           "ToolMajor", "ToolMinor", "Orientation"};
    for (std::size_t axis = 0; axis < names.size(); ++axis) {
        const auto current = std::string("AMotionEvent_get") + names[axis];
        const auto historical = std::string("AMotionEvent_getHistorical") + names[axis];
        const float offset = axis == 0 ? 11.0F : axis == 1 ? 13.0F : 0.0F;
        const auto expected = static_cast<float>(axis) + 10.5F + offset;
        CHECK(std::bit_cast<float>(f.Call("libandroid.so", current, {event, 1})) == expected);
        CHECK(std::bit_cast<float>(FastBoundaryCall(f, "libandroid.so", current, {event, 1})) == expected);
        CHECK(std::bit_cast<float>(f.Call("libandroid.so", historical, {event, 1, 0})) ==
              (axis == 0 ? 111.0F : expected));
    }
    CHECK(std::bit_cast<float>(f.Call("libandroid.so", "AMotionEvent_getRawX", {event, 1})) == 10.5F);
    CHECK(std::bit_cast<float>(f.Call("libandroid.so", "AMotionEvent_getRawY", {event, 1})) == 11.5F);
    CHECK(std::bit_cast<float>(f.Call("libandroid.so", "AMotionEvent_getHistoricalRawX", {event, 1, 0})) == 100.0F);
    CHECK(std::bit_cast<float>(f.Call("libandroid.so", "AMotionEvent_getHistoricalRawY", {event, 1, 0})) == 11.5F);
    CHECK(std::bit_cast<float>(f.Call("libandroid.so", "AMotionEvent_getAxisValue", {event, 63, 1})) == 73.5F);
    CHECK(std::bit_cast<float>(f.Call("libandroid.so", "AMotionEvent_getHistoricalAxisValue", {event, 63, 1, 0})) == 73.5F);
    CHECK(f.Call("libandroid.so", "AMotionEvent_getAxisValue", {event, 64, 1}) == 0);
    const auto wide = [&](const char* name, std::uint64_t expected) {
        f.Call("libandroid.so", name, {event, 0});
        CHECK(f.cpu.GetState().Register(cpu::CoreRegister::r0) == static_cast<std::uint32_t>(expected));
        CHECK(f.cpu.GetState().Register(cpu::CoreRegister::r1) == static_cast<std::uint32_t>(expected >> 32));
        const auto address = f.boundary.Symbols().Lookup("libandroid.so", name);
        REQUIRE(address.has_value());
        std::array<std::uint32_t, 16> registers{};
        registers[0] = event; registers[13] = f.stack.Value();
        cpu::A32HostCallContext context{registers, 1, memory::GuestAddress{address->Value() & ~1U}};
        const auto hook = f.boundary.FastHostCallHook();
        REQUIRE(hook.invoke(hook.userdata, 2, context) == cpu::HostCallResult::handled);
        CHECK(registers[0] == static_cast<std::uint32_t>(expected));
        CHECK(registers[1] == static_cast<std::uint32_t>(expected >> 32));
    };
    wide("AMotionEvent_getDownTime", 0x123456789ULL);
    wide("AMotionEvent_getEventTime", 0x23456789aULL);
    wide("AMotionEvent_getHistoricalEventTime", 0x200000001ULL);
    CHECK_THROWS(f.Call("libandroid.so", "AMotionEvent_getX", {event, 2}));
    CHECK_THROWS(f.Call("libandroid.so", "AMotionEvent_getHistoricalX", {event, 0, 1}));
    CHECK_THROWS(scalar("AKeyEvent_getFlags"));
    f.Call("libandroid.so", "AInputQueue_finishEvent", {2, event, 1});
    CHECK_THROWS(scalar("AInputEvent_getType"));
}

TEST_CASE("BND46 input queues preserve inflight events and failed reads") {
    using namespace ogplay;
    BoundaryFixture f;
    runtime::AndroidBoundaryInput input;
    input.type = runtime::AndroidBoundaryInputType::key;
    input.action = 0; input.source = 0x101; input.device_id = -1;
    input.code = 29; input.scan_code = 4; input.flags = 8; input.meta_state = 3;
    input.repeat_count = 2;
    input.down_time_ns = 0x123456789LL; input.event_time_ns = 0x200000001LL;
    f.boundary.PushInput(input);
    CHECK(f.Call("libandroid.so", "AInputQueue_hasEvents", {2}) == 1);
    CHECK_THROWS(f.Call("libandroid.so", "AInputQueue_getEvent", {2, 0}));
    REQUIRE(f.Call("libandroid.so", "AInputQueue_getEvent", {2, f.output.Value()}) == 0);
    const auto first = f.bus.Read32(f.output);
    input.code = 30;
    f.boundary.PushInput(input);
    REQUIRE(f.Call("libandroid.so", "AInputQueue_getEvent", {2, f.output.Value()}) == 0);
    const auto second = f.bus.Read32(f.output);
    CHECK(first != second);
    CHECK(f.Call("libandroid.so", "AKeyEvent_getKeyCode", {first}) == 29);
    CHECK(f.Call("libandroid.so", "AKeyEvent_getKeyCode", {second}) == 30);
    CHECK(f.Call("libandroid.so", "AKeyEvent_getAction", {first}) == 0);
    CHECK(f.Call("libandroid.so", "AKeyEvent_getFlags", {first}) == 8);
    CHECK(f.Call("libandroid.so", "AKeyEvent_getScanCode", {first}) == 4);
    CHECK(f.Call("libandroid.so", "AKeyEvent_getMetaState", {first}) == 3);
    CHECK(f.Call("libandroid.so", "AKeyEvent_getRepeatCount", {first}) == 2);
    CHECK(f.Call("libandroid.so", "AInputEvent_getSource", {first}) == 0x101);
    CHECK(f.Call("libandroid.so", "AInputEvent_getDeviceId", {first}) == UINT32_MAX);
    CHECK(f.Call("libandroid.so", "AKeyEvent_getDownTime", {first}) == 0x23456789U);
    CHECK(f.cpu.GetState().Register(cpu::CoreRegister::r1) == 1);
    CHECK(f.Call("libandroid.so", "AKeyEvent_getEventTime", {first}) == 1);
    CHECK(f.cpu.GetState().Register(cpu::CoreRegister::r1) == 2);
    CHECK_THROWS(f.Call("libandroid.so", "AMotionEvent_getPointerCount", {first}));
    CHECK(f.Call("libandroid.so", "AInputQueue_hasEvents", {2}) == 0);
    CHECK(static_cast<std::int32_t>(f.Call("libandroid.so", "AInputQueue_getEvent", {2, f.output.Value()})) == -11);
    f.Call("libandroid.so", "AInputQueue_finishEvent", {2, second, 0});
    CHECK(f.Call("libandroid.so", "AKeyEvent_getKeyCode", {first}) == 29);
    CHECK_THROWS(f.Call("libandroid.so", "AInputQueue_preDispatchEvent", {2, second}));
    f.Call("libandroid.so", "AInputQueue_finishEvent", {2, first, 1});
    CHECK_THROWS(f.Call("libandroid.so", "AInputQueue_finishEvent", {2, first, 1}));
}

TEST_CASE("BND46 retired input queues reject stale and foreign event identities") {
    using namespace ogplay;
    BoundaryFixture f;
    const auto first = f.output.Add(256), second = first.Add(256);
    for (const auto activity : {first, second})
        f.boundary.RegisterNativeActivity({activity, activity.Add(128), activity.Add(148), 4, 3});
    f.boundary.SetNativeActivityInput(first, true);
    f.boundary.PushInput({runtime::AndroidBoundaryInputType::pointer_button, 0, 10, 20, true});
    REQUIRE(f.Call("libandroid.so", "AInputQueue_getEvent", {first.Add(148).Value(), f.output.Value()}) == 0);
    const auto old = f.bus.Read32(f.output);
    f.boundary.SetNativeActivityInput(second, true);
    CHECK_THROWS(f.Call("libandroid.so", "AInputQueue_finishEvent", {second.Add(148).Value(), old, 1}));
    CHECK_THROWS(f.Call("libandroid.so", "AInputQueue_preDispatchEvent", {second.Add(148).Value(), old}));
    f.boundary.SetNativeActivityInput(first, false);
    CHECK_THROWS(f.Call("libandroid.so", "AInputEvent_getSource", {old}));
    f.boundary.PushInput({runtime::AndroidBoundaryInputType::pointer_button, 0, 30, 40, true});
    REQUIRE(f.Call("libandroid.so", "AInputQueue_getEvent", {second.Add(148).Value(), f.output.Value()}) == 0);
    const auto current = f.bus.Read32(f.output);
    CHECK(current != old);
    f.boundary.UnregisterNativeActivity(first);
    CHECK(std::bit_cast<float>(f.Call("libandroid.so", "AMotionEvent_getX", {current, 0})) == 30.0F);
    f.boundary.UnregisterNativeActivity(second);
    CHECK_THROWS(f.Call("libandroid.so", "AInputEvent_getSource", {current}));
}

TEST_CASE("BND46 malformed snapshots fail before publication") {
    using namespace ogplay;
    runtime::AndroidBoundaryInput input;
    input.type = runtime::AndroidBoundaryInputType::pointer_motion;
    input.action = 2;
    input.event_time_ns = 100;
    input.pointers = {{}, {}};
    CHECK_THROWS(static_cast<void>(runtime::NormalizeAndroidInput(input))); // duplicate identities
    input.pointers[1].id = 1;
    CHECK_NOTHROW(static_cast<void>(runtime::NormalizeAndroidInput(input)));
    input.action = 5 | (2 << 8);
    CHECK_THROWS(static_cast<void>(runtime::NormalizeAndroidInput(input)));
    input.action = 2;
    input.history = {{101, input.pointers}};
    CHECK_THROWS(static_cast<void>(runtime::NormalizeAndroidInput(input)));
    input.history[0].event_time_ns = 99;
    input.history[0].pointers[1].id = 3;
    CHECK_THROWS(static_cast<void>(runtime::NormalizeAndroidInput(input)));
    input.history.clear();
    input.down_time_ns = 101;
    CHECK_THROWS(static_cast<void>(runtime::NormalizeAndroidInput(input)));
    input.down_time_ns = 0;
    input.pointers.resize(17);
    for (std::size_t i = 0; i < input.pointers.size(); ++i) input.pointers[i].id = static_cast<std::int32_t>(i);
    CHECK_THROWS(static_cast<void>(runtime::NormalizeAndroidInput(input)));
}
