#include <doctest/doctest.h>

#include "ogplay/hal/clock.h"
#include "ogplay/session/session.h"

TEST_CASE("fixed-step clock advances exact deterministic ticks") {
    ogplay::hal::FixedStepClock clock(1'000, 60'000);
    clock.AdvanceFrames(17);
    CHECK(clock.Ticks() == 17'000);
    clock.Reset();
    CHECK(clock.Ticks() == 0);
}

TEST_CASE("empty session has deterministic lifecycle and stepping") {
    ogplay::hal::FixedStepClock clock(1'000, 60'000);
    ogplay::session::Session session(clock);

    const auto opened = session.OpenEmpty();
    CHECK(opened.phase == ogplay::session::Phase::running);
    CHECK(opened.frame == 0);

    const auto stepped = session.Step(6);
    CHECK(stepped.frame == 6);
    CHECK(stepped.guest_ticks == 6'000);
    CHECK(stepped.wall_ms == 100);

    CHECK(session.Pause().phase == ogplay::session::Phase::paused);
    CHECK_THROWS_AS(static_cast<void>(session.Step()), std::logic_error);
    CHECK(session.Resume().phase == ogplay::session::Phase::running);
    CHECK(session.Close().phase == ogplay::session::Phase::idle);
}

TEST_CASE("run until reports both reached and bounded timeout") {
    ogplay::hal::FixedStepClock clock(1, 60);
    ogplay::session::Session session(clock);
    static_cast<void>(session.OpenEmpty());

    const auto timeout = session.UntilFrame(10, 4);
    CHECK_FALSE(timeout.reached);
    CHECK(timeout.state.frame == 4);

    const auto reached = session.UntilFrame(10, 6);
    CHECK(reached.reached);
    CHECK(reached.state.frame == 10);
}
