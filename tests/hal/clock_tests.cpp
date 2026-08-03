#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "ogplay/hal/clock.h"

TEST_CASE("fixed-step clock applies an exact rational rate") {
    ogplay::hal::FixedStepClock clock(1, 1'000);
    clock.SetRate({1, 2});
    clock.AdvanceFrames(1);
    CHECK(clock.Ticks() == 0);
    clock.AdvanceFrames(1);
    CHECK(clock.Ticks() == 1);

    clock.SetRate({3, 2});
    clock.AdvanceFrames(2);
    CHECK(clock.Ticks() == 4);
    CHECK(clock.Rate() == ogplay::hal::ClockRate{3, 2});
}

TEST_CASE("fixed-step clock pause and overflow fail explicitly") {
    ogplay::hal::FixedStepClock clock(2, 1'000);
    clock.Pause();
    CHECK(clock.IsPaused());
    CHECK_THROWS_AS(clock.AdvanceFrames(1), std::logic_error);
    clock.Resume();
    CHECK_FALSE(clock.IsPaused());
    CHECK_THROWS_AS(clock.SetRate({0, 1}), std::invalid_argument);
    CHECK_THROWS_AS(clock.AdvanceFrames(std::numeric_limits<std::uint64_t>::max()),
                    std::overflow_error);
}

TEST_CASE("realtime clock supports deterministic rate pause and reset") {
    using namespace std::chrono_literals;
    ogplay::hal::RealtimeClock::TimePoint now{};
    ogplay::hal::RealtimeClock clock(1'000, [&now] { return now; });

    now += 250ms;
    CHECK(clock.Ticks() == 250);
    clock.SetRate({2, 1});
    now += 125ms;
    CHECK(clock.Ticks() == 500);

    clock.Pause();
    now += 1s;
    CHECK(clock.Ticks() == 500);
    clock.Resume();
    now += 10ms;
    CHECK(clock.Ticks() == 520);

    clock.Reset();
    CHECK(clock.Ticks() == 0);
    CHECK(clock.Rate() == ogplay::hal::ClockRate{});
    CHECK_THROWS_AS(clock.AdvanceFrames(1), std::logic_error);
}
