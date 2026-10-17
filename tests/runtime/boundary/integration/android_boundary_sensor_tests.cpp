#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <bit>
#include <set>
#include "android_boundary_fixture.h"
#include "ogplay/runtime/vfs/vfs.h"

using namespace ogplay;
using namespace ogplay::test::android_boundary;
namespace {
struct SensorFixture {
    runtime::VirtualFileSystem fs;
    std::set<std::uint64_t> retired;
    std::vector<std::array<std::int32_t, 2>> pipes;
    std::vector<std::int32_t> closed;
    BoundaryFixture f{1, nullptr, {}, true, {
        [this](auto fd) { return fs.PipePollEvents(fd); },
        [this](auto tid) { return !retired.contains(tid); },
        [this] {
            const auto p = fs.CreatePipe();
            pipes.push_back({p.read_descriptor, p.write_descriptor});
            return pipes.back();
        },
        [this](auto fd) { fs.Close(fd); closed.push_back(fd); }}};
    std::uint32_t call(const char* symbol, std::array<std::uint32_t, 4> args = {}) {
        return f.Call("libandroid.so", symbol, args);
    }
    std::uint32_t manager() { return call("ASensorManager_getInstance"); }
    std::uint32_t queue(std::uint32_t manager, std::uint32_t looper, bool callback = true) {
        f.bus.Write32(f.stack, 0);
        return call("ASensorManager_createEventQueue", {manager, looper, 2, callback ? 0x12345678U : 0U});
    }
};
}

TEST_CASE("APS-11 sensor empty catalog and inert callback queue follow API19") {
    SensorFixture s;
    const auto manager = s.manager();
    REQUIRE(manager != 0);
    CHECK(s.manager() == manager);
    CHECK(s.call("ALooper_forThread") == 0);
    CHECK_THROWS(s.queue(manager, 0));
    CHECK(s.pipes.empty());
    const auto looper = s.call("ALooper_prepare", {1});
    CHECK(s.call("ALooper_forThread") == looper);
    const auto queue = s.queue(manager, looper);
    REQUIRE(queue != 0);
    REQUIRE(s.pipes.size() == 1);
    CHECK(s.fs.PipePollEvents(s.pipes[0][0]) == std::optional<std::uint32_t>{0});
    const auto warnings = s.f.logger.Snapshot(core::LogLevel::warn, "runtime.android.sensor");
    REQUIRE_FALSE(warnings.empty());
    CHECK(warnings.back().message.find("registration rejected") != std::string::npos);
    const auto hits = s.f.ledger.Unimplemented();
    CHECK(std::ranges::any_of(hits, [](const auto& hit) { return hit.id == "runtime.android_boundary_hle"; }));
    CHECK(s.call("ASensorManager_getSensorList", {manager, s.f.output.Value()}) == 0);
    const memory::GuestAddress list{s.f.bus.Read32(s.f.output)};
    REQUIRE_FALSE(list.IsNull());
    CHECK_NOTHROW(s.f.memory.Validate({list, 1}, memory::AccessType::read));
    CHECK(s.call("ASensorManager_getSensorList", {manager, 0}) == 0);
    CHECK_THROWS(s.call("ASensorManager_getSensorList", {manager, 1}));
    for (auto type : {0U, 1U, 2U, 0xffffffffU}) CHECK(s.call("ASensorManager_getDefaultSensor", {manager, type}) == 0);
    CHECK(s.call("ASensorEventQueue_getEvents", {queue, 0, 10}) == 0);
    CHECK(s.call("ASensorEventQueue_getEvents", {queue, s.f.output.Value(), 0}) == 0);
    CHECK(static_cast<std::int32_t>(s.call("ALooper_pollOnce")) == -3);
    CHECK(s.call("ASensorManager_destroyEventQueue", {manager, queue}) == 0);
    CHECK(s.closed.size() == 2);
    CHECK_THROWS(s.call("ASensorManager_destroyEventQueue", {manager, queue}));
    const auto next = s.queue(manager, looper, false);
    CHECK(next > queue);
    // Successful ident registration also remains idle and is removed on close.
    CHECK(static_cast<std::int32_t>(s.call("ALooper_pollOnce")) == -3);
    CHECK(s.call("ASensorManager_destroyEventQueue", {manager, next}) == 0);
    CHECK(s.call("ALooper_removeFd", {looper, static_cast<std::uint32_t>(s.pipes.back()[0])}) == 0);
}

TEST_CASE("APS-11 API19 sensor hasEvents preserves negative idle quirk") {
    SensorFixture s;
    const auto queue = s.queue(s.manager(), s.call("ALooper_prepare", {1}));
    // Mutation guard: replacing the API19 revents!=POLLIN branch with zero
    // must fail this exact assertion (recorded separately in APS-11 evidence).
    CHECK(std::bit_cast<std::int32_t>(s.call("ASensorEventQueue_hasEvents", {queue})) == -1);
    CHECK(s.call("ASensorEventQueue_getEvents", {queue, s.f.output.Value(), 1}) == 0);
}

TEST_CASE("APS-11 sensor rejects invalid handles and retires thread and process resources") {
    SensorFixture s;
    const auto manager = s.manager();
    const auto looper = s.call("ALooper_prepare", {1});
    const auto queue = s.queue(manager, looper);
    for (const auto* method : {"ASensor_getName", "ASensor_getVendor", "ASensor_getType", "ASensor_getResolution", "ASensor_getMinDelay"}) {
        CHECK_THROWS(s.call(method, {0}));
        CHECK_THROWS(s.call(method, {123}));
    }
    for (const auto* method : {"ASensorEventQueue_enableSensor", "ASensorEventQueue_disableSensor", "ASensorEventQueue_setEventRate"})
        CHECK_THROWS(s.call(method, {queue, 123, 100}));
    for (const auto* method : {"ASensorEventQueue_getEvents", "ASensorEventQueue_hasEvents", "ASensorEventQueue_enableSensor"})
        CHECK_THROWS(s.call(method, {123, 123, 1}));
    CHECK_THROWS(s.call("ASensorManager_getDefaultSensor", {123, 1}));
    CHECK(std::ranges::any_of(s.f.ledger.Unimplemented(), [](const auto& hit) { return hit.id == "dexvm.sensor_absent_facade"; }));
    // Own queue on a guest thread without its own Looper, using another live Looper.
    s.f.bus.Write32(s.f.stack, 0);
    const auto other = FastBoundaryCall(s.f, "libandroid.so", "ASensorManager_createEventQueue", {manager, looper, 2, 0}, 20);
    REQUIRE(other > queue);
    s.retired.insert(20);
    CHECK(s.manager() == manager); // sweeps retired queue owners too
    CHECK(s.closed.size() == 2);
    CHECK_THROWS(s.call("ASensorEventQueue_getEvents", {other}));
    s.f.boundary.RetireThreadLooper(1);
    CHECK(s.closed.size() == 4);
    CHECK_THROWS(s.call("ASensorEventQueue_getEvents", {queue}));
    const auto next_looper = s.call("ALooper_prepare", {1});
    const auto last = s.queue(manager, next_looper);
    s.f.boundary.ShutdownLoopers();
    CHECK(s.closed.size() == 6);
    CHECK_THROWS(s.call("ASensorEventQueue_hasEvents", {last}));
    CHECK_THROWS(s.call("ASensorManager_getInstance"));
}
