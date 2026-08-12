#include <stdexcept>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/hal/clock.h"
#include "ogplay/session/lifecycle.h"

namespace {

class RecordingHost final : public ogplay::session::LifecycleFrameHost {
public:
    void Start(const ogplay::session::LifecycleCallbackRoute route) override {
        Record("start", route);
    }
    void InjectInput() override { events.emplace_back("input"); }
    void LifecycleCallback(
        const ogplay::session::LifecycleCallbackRoute route) override {
        Record("lifecycle", route);
    }
    void RenderCallback(
        const ogplay::session::LifecycleCallbackRoute route) override {
        Record("render", route);
    }
    void Present() override { events.emplace_back("present"); }
    void PumpAudio() override { events.emplace_back("audio"); }
    void Schedule() override {
        events.emplace_back("schedule");
        if (fail_schedule) throw std::runtime_error("scheduler failed");
    }
    void UpdateTime(ogplay::hal::Clock& clock) override {
        events.emplace_back("time");
        clock.AdvanceFrames(1);
    }
    void Stop(const ogplay::session::LifecycleCallbackRoute route) override {
        Record("stop", route);
    }

    void Record(const char* operation,
                const ogplay::session::LifecycleCallbackRoute route) {
        events.push_back(std::string(operation) + ":" +
                         std::string(ogplay::session::ToString(route)));
    }

    std::vector<std::string> events;
    bool fail_schedule{};
};

}  // namespace

TEST_CASE("dex activity lifecycle has stable generic callback routes") {
    using CallbackRoute = ogplay::session::LifecycleCallbackRoute;
    using ProfileLifecycle = ogplay::session::ProfileLifecycle;

    const auto surface = ogplay::session::DescribeLifecycle(
        ProfileLifecycle::dex_activity);
    CHECK(surface.startup == CallbackRoute::framework_activity);
    CHECK(surface.frame_lifecycle == CallbackRoute::framework_activity);
    CHECK(surface.render == CallbackRoute::gl_surface_view_renderer);
    CHECK(surface.shutdown == CallbackRoute::framework_activity);

    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::DescribeLifecycle(
            static_cast<ProfileLifecycle>(255))),
        ogplay::session::LifecycleSequenceError);
}

TEST_CASE("dex activity uses the single ordered frame sequence") {
    const auto lifecycle = ogplay::session::ProfileLifecycle::dex_activity;
    ogplay::hal::FixedStepClock clock(1'000, 60'000);
    RecordingHost host;
    ogplay::session::LifecycleFrameRunner runner(lifecycle, clock, host);
    const auto description = runner.Description();

    CHECK(runner.Start().state ==
          ogplay::session::LifecycleRunState::running);
    const auto frame = runner.StepFrame();
    CHECK(frame.frame == 1);
    CHECK(frame.clock_ticks == 1'000);
    CHECK(runner.Stop().state ==
          ogplay::session::LifecycleRunState::stopped);

    const std::vector<std::string> expected{
        "start:" + std::string(ogplay::session::ToString(description.startup)),
        "input",
        "lifecycle:" +
            std::string(ogplay::session::ToString(description.frame_lifecycle)),
        "render:" + std::string(ogplay::session::ToString(description.render)),
        "present", "audio", "schedule", "time",
        "stop:" + std::string(ogplay::session::ToString(description.shutdown)),
    };
    CHECK(host.events == expected);
}

TEST_CASE("lifecycle sequence rejects misuse and makes frame failure sticky") {
    ogplay::hal::FixedStepClock clock(1, 60);
    RecordingHost host;
    ogplay::session::LifecycleFrameRunner runner(
        ogplay::session::ProfileLifecycle::dex_activity, clock, host);

    CHECK_THROWS_AS(static_cast<void>(runner.StepFrame()),
                    ogplay::session::LifecycleSequenceError);
    static_cast<void>(runner.Start());
    CHECK_THROWS_AS(static_cast<void>(runner.Start()),
                    ogplay::session::LifecycleSequenceError);

    host.fail_schedule = true;
    CHECK_THROWS_WITH_AS(static_cast<void>(runner.StepFrame()),
                         "scheduler failed", std::runtime_error);
    CHECK(runner.State().state == ogplay::session::LifecycleRunState::failed);
    CHECK(runner.State().frame == 0);
    CHECK(clock.Ticks() == 0);
    CHECK_THROWS_AS(static_cast<void>(runner.StepFrame()),
                    ogplay::session::LifecycleSequenceError);

    host.fail_schedule = false;
    CHECK(runner.Stop().state == ogplay::session::LifecycleRunState::stopped);
    CHECK_THROWS_AS(static_cast<void>(runner.Stop()),
                    ogplay::session::LifecycleSequenceError);
}
