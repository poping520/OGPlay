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
    bool descriptors_exhausted{};
    BoundaryFixture f{1,
                      nullptr,
                      {},
                      true,
                      {[this](auto fd) { return fs.PipePollEvents(fd); },
                       [this](auto tid) { return !retired.contains(tid); },
                       [this]() -> std::optional<std::array<std::int32_t, 2>> {
                           if (descriptors_exhausted) return std::nullopt;
                           const auto p = fs.CreatePipe();
                           pipes.push_back({p.read_descriptor, p.write_descriptor});
                           return pipes.back();
                       },
                       [this](auto fd) {
                           fs.Close(fd);
                           closed.push_back(fd);
                       },
                       {},
                       [this](auto fd, bool signal) {
                           std::array<std::byte, 1> byte{};
                           if (signal)
                               static_cast<void>(fs.Write(fd, byte));
                           else
                               static_cast<void>(fs.Read(fd, byte));
                       }}};

    std::uint32_t call(const char* symbol, std::array<std::uint32_t, 4> args = {}) {
        return f.Call("libandroid.so", symbol, args);
    }

    std::uint32_t manager() { return call("ASensorManager_getInstance"); }

    std::uint32_t queue(std::uint32_t manager, std::uint32_t looper, bool callback = true) {
        f.bus.Write32(f.stack, 0);
        return call("ASensorManager_createEventQueue", {manager, looper, 2, callback ? 0x12345678U : 0U});
    }
};
}  // namespace

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
    for (auto type : {0U, 1U, 2U, 0xffffffffU})
        CHECK(s.call("ASensorManager_getDefaultSensor", {manager, type}) == 0);
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

TEST_CASE("APS-11 API19 sensor hasEvents preserves negative idle result") {
    SensorFixture s;
    const auto queue = s.queue(s.manager(), s.call("ALooper_prepare", {1}));
    // Mutation guard: replacing the API19 revents!=POLLIN branch with zero
    // must fail this exact assertion (recorded separately in APS-11 evidence).
    CHECK(std::bit_cast<std::int32_t>(s.call("ASensorEventQueue_hasEvents", {queue})) == -1);
    CHECK(s.call("ASensorEventQueue_getEvents", {queue, s.f.output.Value(), 1}) == 0);
}

TEST_CASE("APS-11 sensor rejects invalid handles and retires thread and process resources") {
    SensorFixture s;
    const auto directory = runtime::StaticAndroidSensorDirectory();
    std::int64_t now = 60'000'000'000;
    s.f.boundary.SetSensorDirectory(directory, runtime::MakeStaticAndroidSensorSource(directory, [&] { return now; }));
    const auto manager = s.manager();
    const auto sensor = s.call("ASensorManager_getDefaultSensor", {manager, 1});
    const auto looper = s.call("ALooper_prepare", {1});
    const auto queue = s.queue(manager, looper, false);
    for (const auto* method :
         {"ASensor_getName", "ASensor_getVendor", "ASensor_getType", "ASensor_getResolution", "ASensor_getMinDelay"}) {
        CHECK_THROWS(s.call(method, {0}));
        CHECK_THROWS(s.call(method, {123}));
    }
    for (const auto* method :
         {"ASensorEventQueue_enableSensor", "ASensorEventQueue_disableSensor", "ASensorEventQueue_setEventRate"})
        CHECK_THROWS(s.call(method, {queue, 123, 100}));
    for (const auto* method :
         {"ASensorEventQueue_getEvents", "ASensorEventQueue_hasEvents", "ASensorEventQueue_enableSensor"})
        CHECK_THROWS(s.call(method, {123, 123, 1}));
    CHECK_THROWS(s.call("ASensorManager_getDefaultSensor", {123, 1}));
    // Own queue on a guest thread without its own Looper, using another live Looper.
    s.f.bus.Write32(s.f.stack, 0);
    const auto other =
        FastBoundaryCall(s.f, "libandroid.so", "ASensorManager_createEventQueue", {manager, looper, 2, 0}, 20);
    REQUIRE(other > queue);
    s.retired.insert(20);
    CHECK(s.manager() == manager);  // sweeps retired queue owners too
    s.f.boundary.RetireThreadLooper(20);
    CHECK(s.closed.empty());
    CHECK(s.fs.PipePollEvents(s.pipes[1][0]) == std::optional<std::uint32_t>{0});
    CHECK(s.call("ASensorEventQueue_enableSensor", {other, sensor}) == 0);
    CHECK(s.call("ASensorEventQueue_setEventRate", {other, sensor, 5000}) == 0);
    CHECK(std::bit_cast<std::int32_t>(s.call("ASensorEventQueue_hasEvents", {other})) == -1);
    now += 5'000'000;
    CHECK(s.call("ASensorEventQueue_hasEvents", {other}) == 1);
    CHECK(s.call("ASensorEventQueue_getEvents", {other, s.f.output.Value(), 1}) == 1);
    CHECK(s.call("ASensorEventQueue_disableSensor", {other, sensor}) == 0);
    s.f.boundary.RetireThreadLooper(1);
    CHECK(s.closed.empty());
    CHECK(s.call("ALooper_removeFd", {looper, static_cast<std::uint32_t>(s.pipes[0][0])}) == 0);
    CHECK(std::bit_cast<std::int32_t>(s.call("ALooper_pollOnce")) == -4);
    const auto next_looper = s.call("ALooper_prepare", {1});
    CHECK(s.call("ASensorEventQueue_enableSensor", {queue, sensor}) == 0);
    CHECK(s.call("ASensorEventQueue_setEventRate", {queue, sensor, 5000}) == 0);
    now += 5'000'000;
    CHECK(s.call("ASensorEventQueue_hasEvents", {queue}) == 1);
    CHECK(s.fs.PipePollEvents(s.pipes[0][0]) == std::optional<std::uint32_t>{1});
    CHECK(std::bit_cast<std::int32_t>(s.call("ALooper_pollOnce")) == -3);
    CHECK(s.call("ASensorEventQueue_getEvents", {queue, s.f.output.Value(), 1}) == 1);
    CHECK(std::bit_cast<std::int32_t>(s.call("ASensorEventQueue_hasEvents", {queue})) == -1);
    CHECK(s.call("ASensorEventQueue_disableSensor", {queue, sensor}) == 0);
    CHECK(s.call("ASensorManager_destroyEventQueue", {manager, other}) == 0);
    CHECK(s.closed.size() == 2);
    CHECK(s.fs.PipePollEvents(s.pipes[1][0]) == std::optional<std::uint32_t>{16});
    CHECK_THROWS(s.call("ASensorManager_destroyEventQueue", {manager, other}));
    CHECK(s.call("ASensorManager_destroyEventQueue", {manager, queue}) == 0);
    CHECK(s.closed.size() == 4);
    CHECK(s.fs.PipePollEvents(s.pipes[0][0]) == std::optional<std::uint32_t>{16});
    CHECK_THROWS(s.call("ASensorManager_destroyEventQueue", {manager, queue}));
    CHECK_THROWS(s.call("ALooper_wake", {looper}));  // last queue released the retired record
    CHECK_THROWS(s.call("ASensorEventQueue_getEvents", {queue}));
    const auto last = s.queue(manager, next_looper);
    s.f.boundary.ShutdownLoopers();
    CHECK(s.closed.size() == 6);
    CHECK_THROWS(s.call("ASensorEventQueue_hasEvents", {last}));
    CHECK_THROWS(s.call("ASensorManager_getInstance"));
}

TEST_CASE("APS-11 Java sensor queues still close with their MessageQueue thread") {
    SensorFixture s;
    auto& queues = s.f.boundary.SensorQueues();
    const auto queue = queues.OpenSensorQueue(20, [] {});
    const auto looper = FastBoundaryCall(s.f, "libandroid.so", "ALooper_forThread", {}, 20);
    SUBCASE("explicit retirement") {
        s.f.boundary.RetireThreadLooper(20);
    }
    SUBCASE("thread liveness sweep") {
        s.retired.insert(20);
        s.manager();
    }
    CHECK(s.closed.size() == 2);
    CHECK(s.fs.PipePollEvents(s.pipes[0][0]) == std::optional<std::uint32_t>{16});
    CHECK_FALSE(queues.EnableSensor(queue, 1, 5000));
    CHECK_THROWS(s.call("ALooper_wake", {looper}));
}

TEST_CASE("APS-11 sensor queue live budget returns NULL without pipe or Looper reference leaks") {
    SensorFixture s;
    const auto manager = s.manager();
    const auto looper = s.call("ALooper_prepare", {1});
    auto& queues = s.f.boundary.SensorQueues();
    const auto java = queues.OpenSensorQueue(1, [] {});
    std::vector<std::uint32_t> live;
    for (std::size_t i = 1; i < 1024; ++i) {
        live.push_back(s.queue(manager, looper, false));
        REQUIRE(live.back() != 0);
    }
    CHECK(s.queue(manager, looper, false) == 0);
    CHECK(s.queue(manager, looper, false) == 0);
    REQUIRE(s.pipes.size() == 1024);
    CHECK(s.closed.empty());
    const auto warnings = s.f.logger.Snapshot(core::LogLevel::warn, "runtime.android.sensor");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message == "sensor queue live budget exhausted");
    CHECK(s.f.logger.SuppressedCount("runtime.android.sensor", warnings[0].message) == 1);
    const auto hits = s.f.ledger.Unimplemented();
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].id == "runtime.android_boundary_hle");
    CHECK(hits[0].count == 2);
    CHECK_THROWS(queues.OpenSensorQueue(1, [] {}));  // Java exhaustion still throws
    CHECK(s.call("ASensorManager_destroyEventQueue", {manager, live.back()}) == 0);
    const auto replacement = s.queue(manager, looper, false);
    REQUIRE(replacement > live.back());  // no early reuse of stale handles
    live.back() = replacement;
    for (auto queue : live)
        CHECK(s.call("ASensorManager_destroyEventQueue", {manager, queue}) == 0);
    queues.CloseSensorQueue(java);
    CHECK(s.closed.size() == s.pipes.size() * 2);
    s.f.boundary.RetireThreadLooper(1);
    CHECK_THROWS(s.call("ALooper_wake", {looper}));
}

TEST_CASE("APS-11 sensor pipe descriptor exhaustion returns NULL and leaves no Looper reference") {
    SensorFixture s;
    const auto manager = s.manager();
    const auto looper = s.call("ALooper_prepare", {1});
    s.descriptors_exhausted = true;
    CHECK(s.queue(manager, looper, false) == 0);
    CHECK(s.pipes.empty());
    CHECK(s.closed.empty());
    const auto warnings = s.f.logger.Snapshot(core::LogLevel::warn, "runtime.android.sensor");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message == "sensor queue VFS descriptors exhausted");
    const auto hits = s.f.ledger.Unimplemented();
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].id == "runtime.android_boundary_hle");
    CHECK(hits[0].count == 1);
    CHECK_THROWS(s.f.boundary.SensorQueues().OpenSensorQueue(1, [] {}));
    s.f.boundary.RetireThreadLooper(1);
    CHECK_THROWS(s.call("ALooper_wake", {looper}));
    s.descriptors_exhausted = false;
    const auto queue = s.queue(manager, s.call("ALooper_prepare", {1}), false);
    CHECK(queue == runtime::guest_layout::kSensorQueueHandleBegin);
    CHECK(s.call("ASensorManager_destroyEventQueue", {manager, queue}) == 0);
    CHECK(s.closed.size() == 2);
}

TEST_CASE("APS-11 sensor queue handles wrap and skip occupied slots") {
    SensorFixture s;
    const auto manager = s.manager();
    const auto looper = s.call("ALooper_prepare", {1});
    const auto occupied = s.queue(manager, looper, false);
    const auto vacant = s.queue(manager, looper, false);
    REQUIRE(occupied == runtime::guest_layout::kSensorQueueHandleBegin);
    REQUIRE(vacant == occupied + 4);
    CHECK(s.call("ASensorManager_destroyEventQueue", {manager, vacant}) == 0);
    // Exercise the real shared cursor through Java's direct queue interface,
    // without exposing private state or adding a test-only allocation hook.
    auto& queues = s.f.boundary.SensorQueues();
    for (auto expected = vacant + 4; expected < runtime::guest_layout::kAssetDirectoryHandleBegin; expected += 4) {
        const auto queue = queues.OpenSensorQueue(1, [] {});
        REQUIRE(queue == expected);
        queues.CloseSensorQueue(queue);
    }
    const auto reused = s.queue(manager, looper, false);
    CHECK(reused == vacant);
    CHECK(s.call("ASensorManager_destroyEventQueue", {manager, reused}) == 0);
    CHECK(s.call("ASensorManager_destroyEventQueue", {manager, occupied}) == 0);
    CHECK(s.closed.size() == s.pipes.size() * 2);
    s.f.boundary.RetireThreadLooper(1);
    CHECK_THROWS(s.call("ALooper_wake", {looper}));
}

TEST_CASE("Static motion sensor events follow the supplied Clock and wake the NDK Looper") {
    SensorFixture s;
    const auto directory = runtime::StaticAndroidSensorDirectory();
    std::int64_t now = 60'000'000'000;
    s.f.boundary.SetSensorDirectory(directory, runtime::MakeStaticAndroidSensorSource(directory, [&] { return now; }));
    const auto manager = s.manager();
    CHECK(s.call("ASensorManager_getSensorList", {manager, s.f.output.Value()}) == 7);
    const memory::GuestAddress list{s.f.bus.Read32(s.f.output)};
    const auto sensor = s.call("ASensorManager_getDefaultSensor", {manager, 1});
    REQUIRE(sensor == s.f.bus.Read32(list));
    const auto queue = s.queue(manager, s.call("ALooper_prepare", {1}), false);
    CHECK(s.call("ASensorEventQueue_enableSensor", {queue, sensor}) == 0);
    CHECK(s.call("ASensorEventQueue_setEventRate", {queue, sensor, 100}) == 0);
    CHECK(std::bit_cast<std::int32_t>(s.call("ALooper_pollOnce")) == -3);  // activation alone yields no sample
    now += 5'000'000;
    CHECK(s.call("ALooper_pollOnce") == 2);
    CHECK(s.call("ASensorEventQueue_getEvents", {queue, s.f.output.Value(), 4}) == 1);
    now += 15'000'000;
    CHECK(s.call("ALooper_pollOnce") == 2);
    CHECK(s.call("ASensorEventQueue_getEvents", {queue, s.f.output.Value(), 4}) == 3);
    std::array<runtime::AndroidSensorSample, 3> samples;
    s.f.memory.Read(s.f.output, std::as_writable_bytes(std::span(samples)), 1);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        CHECK(samples[i].timestamp == now - static_cast<std::int64_t>(samples.size() - i - 1) * 5'000'000);
        CHECK(samples[i].values[2] == doctest::Approx(9.80665F));
    }
    CHECK(s.call("ASensorEventQueue_disableSensor", {queue, sensor}) == 0);
    now += 1'000'000'000;
    CHECK(std::bit_cast<std::int32_t>(s.call("ALooper_pollOnce")) == -3);
}

TEST_CASE("Static sensor streams report first one sampling period after activation") {
    const auto directory = runtime::StaticAndroidSensorDirectory();
    std::int64_t now = 60'000'000'000;
    const auto source = runtime::MakeStaticAndroidSensorSource(directory, [&] { return now; });
    const auto stream = source->OpenStream();
    std::array<runtime::AndroidSensorSample, 8> samples;
    const auto first_timestamp = [&](std::int64_t at) {
        now = at - 1;
        CHECK_FALSE(stream->HasEvents());
        CHECK(stream->Read(samples) == 0);
        now = at;
        REQUIRE(stream->Read(samples) == 1);
        return samples[0].timestamp;
    };
    const auto enabled = now;
    REQUIRE(stream->Enable(1, 20'000));
    CHECK_FALSE(stream->HasEvents());
    SUBCASE("requested period") {
        CHECK(first_timestamp(enabled + 20'000'000) == enabled + 20'000'000);
    }
    SUBCASE("period shortened before the first sample") {
        now += 2'000'000;
        REQUIRE(stream->SetPeriod(1, 10'000));
        CHECK(first_timestamp(enabled + 10'000'000) == enabled + 10'000'000);
    }
    SUBCASE("period lengthened through a repeated enable") {
        now += 2'000'000;
        REQUIRE(stream->Enable(1, 40'000));
        CHECK(first_timestamp(enabled + 40'000'000) == enabled + 40'000'000);
    }
    SUBCASE("reactivation starts a new period") {
        now += 19'000'000;
        REQUIRE(stream->Disable(1));
        now += 100'000'000;
        const auto again = now;
        REQUIRE(stream->Enable(1, 20'000));
        CHECK(first_timestamp(again + 20'000'000) == again + 20'000'000);
    }
    SUBCASE("sampling continues in phase after the first sample") {
        CHECK(first_timestamp(enabled + 20'000'000) == enabled + 20'000'000);
        CHECK(first_timestamp(enabled + 40'000'000) == enabled + 40'000'000);
    }
}
