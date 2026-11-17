#include <doctest/doctest.h>
#include "../../resources_test_support.h"
#include "../../native_library_fixture.h"
#include "runtime/integration/dexvm_android/rom_window.h"

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
#include "ogplay/runtime/dexvm/guest_memory.h"
#include "ogplay/runtime/integration/apk_assets.h"
#include "ogplay/runtime/integration/native_activity_runtime.h"
#include "ogplay/runtime/integration/dexvm_android.h"

using namespace ogplay::test::android_boundary;

namespace {

class BoundaryAssetSource final : public ogplay::runtime::AndroidAssetSource {
public:
    std::uint64_t length{3}, start{123};
    bool compressed{};
    mutable std::size_t largest_read{}, descriptor_opens{}, descriptor_closes{};

    std::uint64_t Length() const override { return length; }

    std::size_t ReadAt(std::uint64_t offset, std::span<std::byte> output) const override {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), length - offset));
        largest_read = std::max(largest_read, count);
        const std::array payload{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
        for (std::size_t i = 0; i < count; ++i)
            output[i] = payload[(offset + i) % payload.size()];
        return count;
    }

    ogplay::runtime::AssetFileDescriptor OpenFileDescriptor() const override {
        if (compressed) return {};
        ++descriptor_opens;
        return {42, start, length};
    }

    void CloseFileDescriptor(std::int32_t) const override { ++descriptor_closes; }
};

class BoundaryAssetManager final : public ogplay::runtime::AndroidAssetManagerSource {
public:
    std::vector<ogplay::runtime::AndroidAssetDirectoryEntry> ListDirectory(std::string_view) override {
        return {{"folder", false}, {"hello", true}, {"other", true}};
    }

    std::shared_ptr<BoundaryAssetSource> source = std::make_shared<BoundaryAssetSource>();

    std::shared_ptr<const ogplay::runtime::AndroidAssetSource> Open(std::string_view name) override {
        return name == "hello" ? source : nullptr;
    }
};

}  // namespace

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
    std::array<std::byte, 3> actual{};
    f.memory.Read(f.output.Add(32), actual);
    CHECK(actual == std::array{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}});
    CHECK(f.Call("libandroid.so", "AAsset_read", {asset, f.output.Add(32).Value(), 8}) == 0);
    f.Call("libandroid.so", "AAsset_close", {asset});
    CHECK_THROWS(f.Call("libandroid.so", "AAsset_getLength", {asset}));
    const auto window = f.boundary.SetNativeActivityWindow(activity, true);
    CHECK(f.Call("libandroid.so", "ANativeWindow_getWidth", {window.Value()}) == 4);
    CHECK(f.Call("libandroid.so", "ANativeWindow_getHeight", {window.Value()}) == 3);
    CHECK(f.Call("libandroid.so", "ANativeWindow_setBuffersGeometry", {window.Value(), 0, 0, 1}) == 0);
    CHECK(static_cast<std::int32_t>(
              f.Call("libandroid.so", "ANativeWindow_setBuffersGeometry", {window.Value(), 8, 3, 1})) == -22);
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

TEST_CASE("P3 targeted NDK input completion retains the queue and runs outside its lock") {
    using namespace ogplay;
    BoundaryFixture f;
    const auto activity = f.output.Add(256), queue = activity.Add(148);
    CHECK_THROWS(f.boundary.PushQueueInput(queue, {runtime::AndroidBoundaryInputType::key, 29, 0, 0, true}, {}));
    f.boundary.RegisterNativeActivity({activity, activity.Add(128), queue, 4, 3});
    f.boundary.SetNativeActivityInput(activity, true);
    int completed = 0;
    bool handled = false;
    f.boundary.PushQueueInput(queue, {runtime::AndroidBoundaryInputType::key, 29, 0, 0, true}, [&](bool result) {
        ++completed;
        handled = result;
        // Reentering the queue proves that the native completion released its lock.
        f.boundary.PushQueueInput(queue, {runtime::AndroidBoundaryInputType::key, 30, 0, 0, true}, {});
    });
    CHECK(completed == 0);
    REQUIRE(f.Call("libandroid.so", "AInputQueue_getEvent", {queue.Value(), f.output.Value()}) == 0);
    const auto event = f.bus.Read32(f.output);
    CHECK(f.Call("libandroid.so", "AKeyEvent_getKeyCode", {event}) == 29);
    f.Call("libandroid.so", "AInputQueue_finishEvent", {queue.Value(), event, 1});
    CHECK(completed == 1);
    CHECK(handled);
    REQUIRE(f.Call("libandroid.so", "AInputQueue_getEvent", {queue.Value(), f.output.Value()}) == 0);
    const auto next = f.bus.Read32(f.output);
    CHECK(f.Call("libandroid.so", "AKeyEvent_getKeyCode", {next}) == 30);
    f.Call("libandroid.so", "AInputQueue_finishEvent", {queue.Value(), next, 0});
    runtime::AndroidBoundaryInput pre{runtime::AndroidBoundaryInputType::key, 31, 0, 0, true};
    pre.flags = runtime::kAndroidKeyPreDispatchFlag;
    int pre_completed = 0;
    f.boundary.PushQueueInput(queue, pre, [&](bool result) {
        CHECK_FALSE(result);
        ++pre_completed;
        f.boundary.PushQueueInput(queue, {runtime::AndroidBoundaryInputType::key, 32, 0, 0, true}, {});
    });
    REQUIRE(f.Call("libandroid.so", "AInputQueue_getEvent", {queue.Value(), f.output.Value()}) == 0);
    const auto pre_event = f.bus.Read32(f.output);
    CHECK(f.Call("libandroid.so", "AKeyEvent_getFlags", {pre_event}) ==
          static_cast<unsigned>(runtime::kAndroidKeyPreDispatchFlag));
    CHECK(f.Call("libandroid.so", "AInputQueue_preDispatchEvent", {queue.Value(), pre_event}) == 1);
    CHECK(pre_completed == 1);
    CHECK_THROWS(f.Call("libandroid.so", "AKeyEvent_getFlags", {pre_event}));
    REQUIRE(f.Call("libandroid.so", "AInputQueue_getEvent", {queue.Value(), f.output.Value()}) == 0);
    const auto normal = f.bus.Read32(f.output);
    CHECK(f.Call("libandroid.so", "AInputQueue_preDispatchEvent", {queue.Value(), normal}) == 0);
    f.Call("libandroid.so", "AInputQueue_finishEvent", {queue.Value(), normal, 0});
    f.boundary.SetNativeActivityInput(activity, false);
    CHECK_THROWS(f.boundary.PushQueueInput(queue, {runtime::AndroidBoundaryInputType::key, 31, 0, 0, true}, {}));
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
    CHECK(f.Call("libandroid.so", "AAsset_openFileDescriptor",
                 {asset, f.output.Add(64).Value(), f.output.Add(68).Value()}) == 42);
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

TEST_CASE("API19 NDK asset off64_t uses aligned arguments and two result registers") {
    using namespace ogplay;
    BoundaryFixture f;
    const auto source = std::make_shared<BoundaryAssetManager>();
    source->source->length = 0x300000015ULL;
    source->source->start = 0x200000007ULL;
    const auto manager = f.boundary.RegisterAssetManager(source);
    WriteGuestString(f, f.output, "hello");
    const auto asset = f.Call("libandroid.so", "AAssetManager_open", {manager.Value(), f.output.Value(), 2});
    REQUIRE(asset);
    const auto check_wide = [&](const char* name, std::span<const std::uint32_t> args, std::uint64_t expected) {
        const auto target = f.boundary.Symbols().Lookup("libandroid.so", name);
        REQUIRE(target);
        CHECK(BoundaryCallAddress(f, target->Value(), args) == static_cast<std::uint32_t>(expected));
        CHECK(f.cpu.GetState().Register(cpu::CoreRegister::r1) == static_cast<std::uint32_t>(expected >> 32));
        std::array<std::uint32_t, 16> registers{};
        const auto reg_words = std::min<std::size_t>(args.size(), 4);
        std::copy_n(args.begin(), reg_words, registers.begin());
        for (std::size_t i = reg_words; i < args.size(); ++i)
            f.bus.Write32(f.stack.Add((i - 4) * 4), args[i]);
        registers[13] = f.stack.Value();
        cpu::A32HostCallContext call{registers, 1, memory::GuestAddress{target->Value() & ~1U}};
        const auto hook = f.boundary.FastHostCallHook();
        REQUIRE(hook.invoke(hook.userdata, 2, call) == cpu::HostCallResult::handled);
        CHECK(registers[0] == static_cast<std::uint32_t>(expected));
        CHECK(registers[1] == static_cast<std::uint32_t>(expected >> 32));
    };
    check_wide("AAsset_getLength64", std::array{asset}, 0x300000015ULL);
    check_wide("AAsset_seek64", std::array{asset, 0xcafeU, 5U, 1U, 0U}, 0x100000005ULL);
    check_wide("AAsset_getRemainingLength64", std::array{asset}, 0x200000010ULL);
    check_wide("AAsset_seek64", std::array{asset, 0xcafeU, UINT32_MAX, UINT32_MAX, 2U}, 0x300000014ULL);
    check_wide("AAsset_seek64", std::array{asset, 0U, 0U, 4U, 0U}, UINT64_MAX);
    check_wide("AAsset_seek64", std::array{asset, 0U, 0U, 0U, 3U}, UINT64_MAX);
    check_wide("AAsset_getRemainingLength64", std::array{asset}, 1);

    const auto start = f.output.Add(32);
    const auto length = start.Add(8);
    f.bus.Write32(start, 0xabcdef00);
    CHECK_THROWS(f.Call("libandroid.so", "AAsset_openFileDescriptor64",
                        {asset, start.Value(), f.output.Add(f.memory.PageSize() - 4).Value()}));
    CHECK(source->source->descriptor_opens == 0);
    CHECK(f.bus.Read32(start) == 0xabcdef00);
    CHECK(f.Call("libandroid.so", "AAsset_openFileDescriptor64", {asset, start.Value(), length.Value()}) == 42);
    const auto read64 = [&](memory::GuestAddress ptr) {
        return (std::uint64_t{f.bus.Read32(ptr.Add(4))} << 32) | f.bus.Read32(ptr);
    };
    CHECK(read64(start) == source->source->start);
    CHECK(read64(length) == source->source->length);
    source->source->compressed = true;
    CHECK(f.Call("libandroid.so", "AAsset_openFileDescriptor64", {asset, start.Value(), length.Value()}) == UINT32_MAX);
    CHECK(read64(start) == source->source->start);
    CHECK(read64(length) == source->source->length);
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
    input.flags = 8;
    input.meta_state = 3;
    input.button_state = 2;
    input.edge_flags = 4;
    input.down_time_ns = 0x123456789LL;
    input.event_time_ns = 0x23456789aLL;
    input.x_offset = 11;
    input.y_offset = 13;
    input.x_precision = 0.5F;
    input.y_precision = 0.25F;
    runtime::AndroidInputPointer first, second;
    first.id = 9;
    second.id = 3;
    second.tool_type = 2;
    for (std::size_t i = 0; i < 64; ++i) {
        first.axes[i] = static_cast<float>(i) + 0.5F;
        second.axes[i] = static_cast<float>(i) + 10.5F;
    }
    input.pointers = {first, second};
    first.axes[0] = 90;
    second.axes[0] = 100;
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
    const std::array names{"X",          "Y",         "Pressure",  "Size",       "TouchMajor",
                           "TouchMinor", "ToolMajor", "ToolMinor", "Orientation"};
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
    CHECK(std::bit_cast<float>(f.Call("libandroid.so", "AMotionEvent_getHistoricalAxisValue", {event, 63, 1, 0})) ==
          73.5F);
    CHECK(f.Call("libandroid.so", "AMotionEvent_getAxisValue", {event, 64, 1}) == 0);
    const auto wide = [&](const char* name, std::uint64_t expected) {
        f.Call("libandroid.so", name, {event, 0});
        CHECK(f.cpu.GetState().Register(cpu::CoreRegister::r0) == static_cast<std::uint32_t>(expected));
        CHECK(f.cpu.GetState().Register(cpu::CoreRegister::r1) == static_cast<std::uint32_t>(expected >> 32));
        const auto address = f.boundary.Symbols().Lookup("libandroid.so", name);
        REQUIRE(address.has_value());
        std::array<std::uint32_t, 16> registers{};
        registers[0] = event;
        registers[13] = f.stack.Value();
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
    input.action = 0;
    input.source = 0x101;
    input.device_id = -1;
    input.code = 29;
    input.scan_code = 4;
    input.flags = 8;
    input.meta_state = 3;
    input.repeat_count = 2;
    input.down_time_ns = 0x123456789LL;
    input.event_time_ns = 0x200000001LL;
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
    CHECK_THROWS(static_cast<void>(runtime::NormalizeAndroidInput(input)));  // duplicate identities
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
    for (std::size_t i = 0; i < input.pointers.size(); ++i)
        input.pointers[i].id = static_cast<std::int32_t>(i);
    CHECK_THROWS(static_cast<void>(runtime::NormalizeAndroidInput(input)));
}

TEST_CASE("APS-11 NDK Asset directories cache guest names and retire with their manager") {
    using namespace ogplay;
    BoundaryFixture f;
    const auto manager = f.boundary.RegisterAssetManager(std::make_shared<BoundaryAssetManager>());
    WriteGuestString(f, f.output, "");
    const auto directory = f.Call("libandroid.so", "AAssetManager_openDir", {manager.Value(), f.output.Value()});
    REQUIRE(directory != 0);
    const memory::GuestAddress name{f.Call("libandroid.so", "AAssetDir_getNextFileName", {directory})};
    std::array<std::byte, 6> bytes{};
    f.memory.Read(name, bytes);
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), 5) == "hello");
    CHECK_THROWS(f.memory.Write(name, bytes));
    f.Call("libandroid.so", "AAssetDir_rewind", {directory});
    CHECK_NOTHROW(f.memory.Read(name, bytes));
    const memory::GuestAddress again{f.Call("libandroid.so", "AAssetDir_getNextFileName", {directory})};
    f.memory.Read(again, bytes);
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), 5) == "hello");
    const auto next = f.Call("libandroid.so", "AAssetDir_getNextFileName", {directory});
    REQUIRE(next != 0);
    f.memory.Read(memory::GuestAddress{next}, bytes);
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), 5) == "other");
    CHECK(f.Call("libandroid.so", "AAssetDir_getNextFileName", {directory}) == 0);
    CHECK(f.Call("libandroid.so", "AAssetDir_getNextFileName", {directory}) == 0);
    CHECK_THROWS(f.memory.Read(memory::GuestAddress{next}, bytes));
    f.Call("libandroid.so", "AAssetDir_close", {directory});
    CHECK_THROWS(f.Call("libandroid.so", "AAssetDir_close", {directory}));
    const auto other = f.Call("libandroid.so", "AAssetManager_openDir", {manager.Value(), f.output.Value()});
    CHECK(other > directory);
    const memory::GuestAddress cached{f.Call("libandroid.so", "AAssetDir_getNextFileName", {other})};
    f.boundary.UnregisterAssetManager(manager);
    CHECK_THROWS(f.Call("libandroid.so", "AAssetDir_getNextFileName", {other}));
    CHECK_THROWS(f.memory.Read(cached, bytes));
    const auto manager2 = f.boundary.RegisterAssetManager(std::make_shared<BoundaryAssetManager>());
    const auto last = f.Call("libandroid.so", "AAssetManager_openDir", {manager2.Value(), f.output.Value()});
    f.boundary.SetAssetManagerHooks({});
    CHECK_THROWS(f.Call("libandroid.so", "AAssetDir_getNextFileName", {last}));
}

TEST_CASE("APS-11 NDK Asset getBuffer is lazy stable readonly bounded and released on close") {
    using namespace ogplay;
    BoundaryFixture f;
    const auto source = std::make_shared<BoundaryAssetManager>();
    const auto manager = f.boundary.RegisterAssetManager(source);
    WriteGuestString(f, f.output, "hello");
    const auto open = [&] {
        return f.Call("libandroid.so", "AAssetManager_open", {manager.Value(), f.output.Value(), 2});
    };
    const auto asset = open();
    CHECK(f.Call("libandroid.so", "AAsset_isAllocated", {asset}) == 0);
    CHECK(source->source->largest_read == 0);
    const memory::GuestAddress buffer{f.Call("libandroid.so", "AAsset_getBuffer", {asset})};
    REQUIRE_FALSE(buffer.IsNull());
    CHECK(f.Call("libandroid.so", "AAsset_isAllocated", {asset}) == 1);
    CHECK(source->source->largest_read == 3);
    CHECK(f.Call("libandroid.so", "AAsset_getBuffer", {asset}) == buffer.Value());
    CHECK(f.Call("libandroid.so", "AAsset_getRemainingLength", {asset}) == 3);
    std::array<std::byte, 3> bytes{};
    f.memory.Read(buffer, bytes);
    CHECK(bytes == std::array{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}});
    CHECK_THROWS(f.memory.Write(buffer, bytes));
    f.Call("libandroid.so", "AAsset_close", {asset});
    CHECK_THROWS(f.memory.Read(buffer, bytes));
    CHECK_THROWS(f.Call("libandroid.so", "AAsset_getBuffer", {asset}));
    source->source->length = runtime::kAndroidAssetBufferLimit + 1;
    const auto large = open();
    const auto largest_read = source->source->largest_read;
    CHECK_THROWS(f.Call("libandroid.so", "AAsset_getBuffer", {large}));
    CHECK(source->source->largest_read == largest_read);
    CHECK(std::ranges::any_of(f.ledger.Unimplemented(),
                              [](const auto& hit) { return hit.id == "runtime.asset_manager"; }));
    source->source->length = 0;
    const auto empty = open();
    CHECK(f.Call("libandroid.so", "AAsset_getBuffer", {empty}) != 0);
    source->source->length = 3;
    const auto last = open();
    const memory::GuestAddress last_buffer{f.Call("libandroid.so", "AAsset_getBuffer", {last})};
    f.boundary.SetAssetManagerHooks({});
    CHECK_THROWS(f.memory.Read(last_buffer, bytes));
}

TEST_CASE("APS-11 NDK fromSurface acquires the holder window and respects destroyed generations") {
    using namespace ogplay;
    BoundaryFixture f;

    struct Source {
        BoundaryFixture* fixture;
        bool live{true};
    } source{&f};

    f.boundary.SetSurfaceHooks(
        {&source, +[](void* owner, std::uint64_t, memory::GuestAddress env, runtime::SurfaceJavaReference ref) {
             auto& s = *static_cast<Source*>(owner);
             if (!s.live || env.Value() != 123 || ref.token != 456) return memory::GuestAddress{};
             return s.fixture->boundary.AcquireSurfaceWindow({memory::GuestAddress{789}, 4, 3});
         }});
    CHECK(f.Call("libandroid.so", "ANativeWindow_fromSurface", {123, 0}) == 0);
    CHECK(f.Call("libandroid.so", "ANativeWindow_fromSurface", {123, 1}) == 0);
    const auto window = f.Call("libandroid.so", "ANativeWindow_fromSurface", {123, 456});
    REQUIRE(window != 0);
    CHECK(f.Call("libandroid.so", "ANativeWindow_fromSurface", {123, 456}) == window);
    f.Call("libandroid.so", "ANativeWindow_acquire", {window});
    CHECK(f.Call("libandroid.so", "ANativeWindow_getWidth", {window}) == 4);
    CHECK(f.Call("libandroid.so", "ANativeWindow_setBuffersGeometry", {window, 4, 3, 1}) == 0);
    CHECK(f.Call("libEGL.so", "eglInitialize", {1}) == 1);
    const auto egl = f.Call("libEGL.so", "eglCreateWindowSurface", {1, 2, window, 0});
    REQUIRE(egl != 0);
    f.boundary.UpdateSurfaceWindow({memory::GuestAddress{789}, 8, 6});
    CHECK(f.Call("libandroid.so", "ANativeWindow_getWidth", {window}) == 8);
    CHECK(f.Call("libandroid.so", "ANativeWindow_getHeight", {window}) == 6);
    CHECK(f.Call("libandroid.so", "ANativeWindow_setBuffersGeometry", {window, 8, 6, 1}) == 0);
    source.live = false;
    f.boundary.RetireSurfaceWindow(memory::GuestAddress{789});
    CHECK(f.Call("libandroid.so", "ANativeWindow_fromSurface", {123, 456}) == 0);
    CHECK(f.Call("libEGL.so", "eglCreateWindowSurface", {1, 2, window, 0}) == 0);
    CHECK(f.Call("libEGL.so", "eglGetError") == 0x300b);
    // Three acquired references survive holder retirement; none is implicit.
    for (int i = 0; i < 3; ++i) {
        CHECK(f.Call("libandroid.so", "ANativeWindow_getWidth", {window}) == 8);
        f.Call("libandroid.so", "ANativeWindow_release", {window});
    }
    CHECK_THROWS(f.Call("libandroid.so", "ANativeWindow_getWidth", {window}));
    CHECK_THROWS(f.Call("libandroid.so", "ANativeWindow_release", {window}));
    source.live = true;
    const auto next = f.Call("libandroid.so", "ANativeWindow_fromSurface", {123, 456});
    CHECK(next > window);
    CHECK(f.Call("libEGL.so", "eglCreateWindowSurface", {1, 2, next, 0}) != 0);
    f.boundary.SetSurfaceHooks({});
    CHECK_THROWS(f.Call("libandroid.so", "ANativeWindow_getWidth", {next}));
}

TEST_CASE("APS-11 APK Asset directories enumerate root subdirectory and absent paths through ARM ABI") {
    using namespace ogplay;
    BoundaryFixture f;
    std::vector<std::pair<std::string, std::vector<std::byte>>> entries;
    for (const auto* name : {"assets/z", "assets/a", "assets/sub/", "assets/sub/file", "assets/sub/deeper/item",
                             "assets/sub/other", "res/raw/ignored"})
        entries.emplace_back(name, std::vector<std::byte>{std::byte{42}});
    auto source = std::make_shared<runtime::ResourceAssetManager>(test::resources::Framework(), nullptr);
    CHECK(source->core.addAssetPath(test::resources::OwnedSource(test::resources::Zip(entries))) == 2);
    const auto manager = f.boundary.RegisterAssetManager(source);
    const auto list = [&](const char* path) {
        WriteGuestString(f, f.output, path);
        const auto directory = f.Call("libandroid.so", "AAssetManager_openDir", {manager.Value(), f.output.Value()});
        REQUIRE(directory != 0);
        std::vector<std::string> names;
        for (;;) {
            const memory::GuestAddress name{f.Call("libandroid.so", "AAssetDir_getNextFileName", {directory})};
            if (name.IsNull()) break;
            std::string value;
            for (std::uint32_t i = 0; i < 4096; ++i) {
                const auto byte = f.bus.Read8(name.Add(i));
                if (!byte) break;
                value += static_cast<char>(byte);
            }
            names.push_back(value);
        }
        f.Call("libandroid.so", "AAssetDir_close", {directory});
        return names;
    };
    CHECK(list("") == std::vector<std::string>{"a", "z"});
    CHECK(list("sub") == std::vector<std::string>{"file", "other"});
    for (const auto* path : {"missing", "a", "../sub", "/sub", "sub/../sub", "sub//deeper"})
        CHECK(list(path).empty());
    std::atomic<bool> failed{};
    {
        std::vector<std::jthread> readers;
        for (int i = 0; i < 4; ++i)
            readers.emplace_back([&] {
                for (int j = 0; j < 100; ++j) {
                    const auto asset = source->Open("z");
                    std::byte byte{};
                    if (!asset || asset->ReadAt(0, {&byte, 1}) != 1 || byte != std::byte{42}) failed = true;
                }
            });
    }
    CHECK_FALSE(failed.load());
}

TEST_CASE("API19 NDK OBB scanner validates a bounded VFS footer") {
    using namespace ogplay;
    test::native_library::FixtureProcess f;
    auto& process = *f.process;
    const auto memory = process.GuestMemoryAccess();
    const auto path = memory.allocate(64);
    constexpr char filename[] = "/main.obb";
    memory.write(path, std::as_bytes(std::span(filename)));
    const runtime::BionicHleSymbolProvider symbols(runtime::detail::BuildAndroidBoundarySymbols());
    const auto call = [&](const char* name, std::uint32_t arg) {
        const auto target = symbols.Lookup("libandroid.so", name);
        REQUIRE(target);
        return process.Invoke({*target, {arg, 0, 0, 0}, {}}).return_value;
    };
    constexpr std::string_view package = "org.example.obb";
    constexpr std::size_t footer_start = 65536;
    constexpr std::size_t footer_size = 24 + package.size();
    std::vector<std::byte> file(footer_start + footer_size + 8);
    const auto word = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            file[footer_start + offset + i] = static_cast<std::byte>(value >> (i * 8));
    };
    word(0, 1);
    word(4, 123);
    word(8, 0x80000001);
    word(12, 0x12345678);
    word(16, 0xfedcba98);
    word(20, package.size());
    const auto name_bytes = std::as_bytes(std::span(package));
    std::copy(name_bytes.begin(), name_bytes.end(), file.begin() + footer_start + 24);
    word(footer_size, footer_size);
    word(footer_size + 4, 0x01059983);
    std::size_t read_bytes{};
    f.filesystem.PutReadOnlyFile(filename, file.size(), [&](std::uint64_t offset, std::span<std::byte> output) {
        read_bytes += output.size();
        const auto input = std::span(file).subspan(static_cast<std::size_t>(offset), output.size());
        std::ranges::copy(input, output.begin());
        return output.size();
    });

    const auto info = call("AObbScanner_getObbInfo", path.Value());
    REQUIRE(info);
    CHECK(read_bytes == 32776);
    CHECK(call("AObbInfo_getVersion", info) == 123);
    CHECK(call("AObbInfo_getFlags", info) == 0x80000001);
    const memory::GuestAddress name{call("AObbInfo_getPackageName", info)};
    std::array<char, package.size() + 1> actual{};
    memory.read(name, std::as_writable_bytes(std::span(actual)));
    CHECK(std::string_view(actual.data(), package.size()) == package);
    CHECK(actual.back() == '\0');
    CHECK_THROWS(memory.write(name, name_bytes));
    call("AObbInfo_delete", info);
    CHECK_THROWS(memory.read(name, std::as_writable_bytes(std::span(actual))));
    call("AObbInfo_delete", 0);

    for (const auto& [offset, invalid, valid] : {
             std::array<std::size_t, 3>{footer_size + 4, 0, 0x01059983},
             std::array<std::size_t, 3>{footer_size, 32769, footer_size},
             std::array<std::size_t, 3>{0, 2, 1},
             std::array<std::size_t, 3>{20, package.size() + 1, package.size()},
         }) {
        word(offset, static_cast<std::uint32_t>(invalid));
        CHECK(call("AObbScanner_getObbInfo", path.Value()) == 0);
        word(offset, static_cast<std::uint32_t>(valid));
    }
    bool materialized{};
    f.filesystem.MountLazyReadOnly(runtime::VfsSource::obb, "/archives",
                                   std::array{runtime::VfsLazyMountEntry{"legacy.obb",
                                                                         file.size(),
                                                                         [&] {
                                                                             materialized = true;
                                                                             return file;
                                                                         },
                                                                         {}}});
    constexpr char legacy[] = "/archives/legacy.obb";
    memory.write(path, std::as_bytes(std::span(legacy)));
    CHECK(call("AObbScanner_getObbInfo", path.Value()) == 0);
    CHECK_FALSE(materialized);
    constexpr char missing[] = "/missing.obb";
    memory.write(path, std::as_bytes(std::span(missing)));
    CHECK(call("AObbScanner_getObbInfo", path.Value()) == 0);

    memory.write(path, std::as_bytes(std::span(filename)));
    const auto live_info = call("AObbScanner_getObbInfo", path.Value());
    REQUIRE(live_info);
    const memory::GuestAddress live_name{call("AObbInfo_getPackageName", live_info)};
    process.Stop();
    CHECK_THROWS(memory.read(live_name, std::as_writable_bytes(std::span(actual))));
}

TEST_CASE("API19 NDK storage manager reports device state and records OBB stubs") {
    using namespace ogplay;
    BoundaryFixture f;
    const auto call = [&](const char* name, std::array<std::uint32_t, 4> args = {}) {
        return f.Call("libandroid.so", name, args);
    };
    const auto manager = call("AStorageManager_new");
    REQUIRE(manager);
    WriteGuestString(f, f.output, "/main.obb");
    const auto args = std::array{manager, f.output.Value(), 0U, 0U};
    CHECK(call("AStorageManager_isObbMounted", args) == 0);
    CHECK(call("AStorageManager_getMountedObbPath", args) == 0);
    CHECK(f.ledger.Unimplemented().empty());
    for (const auto* name : {"AStorageManager_mountObb", "AStorageManager_unmountObb"}) {
        const auto target = f.boundary.Symbols().Lookup("libandroid.so", name);
        REQUIRE(target);
        CHECK_THROWS(BoundaryCallAddress(f, target->Value(), std::array{manager, f.output.Value(), 0U, 0U, 0U}));
        CHECK(BoundaryCallAddress(f, target->Value(), std::array{manager, f.output.Value(), 0U, 1U, 0U}) == 0);
        CHECK(BoundaryCallAddress(f, target->Value(), std::array{manager, f.output.Value(), 0U, 1U, 0U}) == 0);
        CHECK(std::ranges::any_of(f.ledger.Unimplemented(),
                                  [&](const auto& hit) { return hit.id == std::string("boundary.android.") + name; }));
    }
    CHECK(f.logger.Snapshot(core::LogLevel::warn, "runtime.android.obb").size() == 2);
    call("AStorageManager_delete", {manager});
    call("AStorageManager_delete", {0});
    CHECK_THROWS(call("AStorageManager_isObbMounted", args));
    CHECK(call("AStorageManager_new") > manager);
}

TEST_CASE("API19 NDK NativeActivity worker commands reach the ROM main queue in order") {
    using namespace ogplay;
    namespace dx = runtime::dexvm;
    using runtime::android_intrinsics::CallAndroidMethod;
    using runtime::android_intrinsics::WindowField;
    test::native_library::OrchestratedApp f(
        "android.app.NativeActivity", true, true, {}, {}, true, dx::InterpreterBackend::switch_dispatch,
        "android.app.Application", {}, {64, 36}, {}, false,
        {.trace_capacity = 16384, .event_mask = dx::DexVmTraceBit(dx::DexVmTraceKind::method_enter)});
    f.app->ActivityLifecycle().SetRealtimeVideoClock(false);
    f.app->StartApplication();
    REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
    auto& process = f.app->NativeProcess();
    auto& vm = f.app->DexVm().Vm();
    const auto activity = f.context->foreground_activity;
    const auto queue = f.context->native_activity->InputQueuePointer(activity);
    REQUIRE_FALSE(queue.IsNull());
    const auto native = queue.Value() - 148;
    const runtime::BionicHleSymbolProvider symbols(runtime::detail::BuildAndroidBoundarySymbols());
    constexpr std::uint64_t worker_tid = 16385;
    const auto post = [&](const char* name, std::uint32_t first = 0, std::uint32_t second = 0) {
        const auto target = symbols.Lookup("libandroid.so", name);
        if (!target) throw std::logic_error("missing NativeActivity command");
        static_cast<void>(process.Invoke({*target, {native, first, second, 0}, {}, worker_tid}));
    };
    const auto attributes = [&] {
        const auto window = CallAndroidMethod(vm, activity, "getWindow", "()Landroid/view/Window;").ref;
        const auto attrs =
            CallAndroidMethod(vm, window, "getAttributes", "()Landroid/view/WindowManager$LayoutParams;").ref;
        return std::pair{WindowField(vm, attrs, "format", "I").bits, WindowField(vm, attrs, "flags", "I").bits};
    };
    // Hold the Java execution lock on main while a distinct guest thread
    // posts work. Completion on the worker must not require Java execution.
    process.PrepareDexVmThread(worker_tid, 0);
    std::future<void> worker;
    f.RunOnMainThread([&] {
        const auto before = attributes();
        worker = std::async(std::launch::async, [&] {
            post("ANativeActivity_setWindowFormat", 4);
            post("ANativeActivity_setWindowFormat", 2);
            post("ANativeActivity_setWindowFlags", 0x80, 0);
            post("ANativeActivity_setWindowFlags", 0x100, 0x80);
            post("ANativeActivity_showSoftInput", 1);
            post("ANativeActivity_hideSoftInput", 0);
        });
        REQUIRE(worker.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        CHECK(attributes() == before);
    });
    worker.get();
    f.RunOnMainThread([&] {
        CHECK(vm.CurrentContextToken() == 1);
        const auto [format, flags] = attributes();
        CHECK(format == 2);
        CHECK((flags & (0x80 | 0x100)) == 0x100);
        std::vector<std::string> methods;
        for (const auto& entry : vm.Trace("Landroid/app/NativeActivity;")) {
            if (entry.class_descriptor != "Landroid/app/NativeActivity;" ||
                entry.kind != dx::DexVmTraceKind::method_enter)
                continue;
            if (entry.method_name == "setWindowFormat" || entry.method_name == "setWindowFlags" ||
                entry.method_name == "showIme" || entry.method_name == "hideIme") {
                CHECK(entry.context_token == vm.CurrentContextToken());
                methods.push_back(entry.method_name);
            }
        }
        CHECK(methods == std::vector<std::string>{"setWindowFormat", "setWindowFormat", "setWindowFlags",
                                                  "setWindowFlags", "showIme", "hideIme"});
    });

    SUBCASE("finish reaches Activity.finish on the owning Looper") {
        worker = std::async(std::launch::async, [&] { post("ANativeActivity_finish"); });
        worker.get();
        process.ReleaseDexVmThread(worker_tid);
        for (int i = 0; i < 8 && f.context->foreground_activity.IsValid(); ++i)
            static_cast<void>(f.app->ActivityLifecycle().StepFrame());
        CHECK_FALSE(f.context->foreground_activity.IsValid());
        CHECK(f.context->native_activity->InputQueuePointer(activity).IsNull());
    }
    SUBCASE("unload drops queued and late commands") {
        // Queue work immediately before unloading inside the same Java turn.
        // The next poll must not apply it to this window.
        f.RunOnMainThread([&] {
            worker = std::async(std::launch::async, [&] { post("ANativeActivity_setWindowFormat", 4); });
            REQUIRE(worker.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
            const auto handle = WindowField(vm, activity, "mNativeHandle", "I").bits;
            const auto type = vm.Linker().ResolveDescriptor("Landroid/app/NativeActivity;");
            const auto method = vm.Linker().FindDirectMethod(type, "unloadNativeCode", "(I)V");
            REQUIRE(method);
            const auto result = vm.Call(
                *method, std::array{dx::VmValue::Ref(activity), dx::VmValue::Int(std::bit_cast<std::int32_t>(handle))});
            REQUIRE_FALSE(result.exception.IsValid());
        });
        worker.get();
        f.RunOnMainThread([&] {
            CHECK(attributes().first == 2);
            CHECK(f.context->native_activity->InputQueuePointer(activity).IsNull());
        });
        auto stale = std::async(std::launch::async, [&] { post("ANativeActivity_finish"); });
        CHECK_THROWS(stale.get());
        CHECK_FALSE(f.logger.Snapshot(core::LogLevel::warn, "runtime.native_activity.command").empty());
        process.ReleaseDexVmThread(worker_tid);
        // The test already called the ROM unload native entry directly.
        static_cast<void>(f.app->Stop(session::DexProcessStopReason::runtime_failure));
    }
}
