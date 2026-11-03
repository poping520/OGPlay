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
    CHECK_THROWS_AS(clock.AdvanceFrames(std::numeric_limits<std::uint64_t>::max()), std::overflow_error);
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

TEST_CASE("frame pacer caps a fast producer to the frame period") {
    CHECK(ogplay::hal::FramePeriodNs(0) == 0);
    CHECK(ogplay::hal::FramePeriodNs(60) == 16'666'666);
    ogplay::hal::FramePacer pacer(ogplay::hal::FramePeriodNs(50));
    CHECK(pacer.Reserve(1'000) == 0);
    // A producer ready 6 ms after each release is released every 20 ms.
    CHECK(pacer.Reserve(6'001'000) == 14'000'000);
    CHECK(pacer.Reserve(26'001'000) == 14'000'000);
    pacer.Reset();
    CHECK(pacer.Reserve(50'000'000) == 0);
    CHECK(pacer.Reserve(51'000'000) == 19'000'000);
}

TEST_CASE("frame pacer never delays a producer slower than the cap and does not catch up") {
    ogplay::hal::FramePacer pacer(20'000'000);
    CHECK(pacer.Reserve(0) == 0);
    // A producer at 40 ms per frame (under its own limit) is never held back.
    for (std::uint64_t now = 40'000'000; now <= 400'000'000; now += 40'000'000)
        CHECK(pacer.Reserve(now) == 0);
    // After a 100 ms stall, the next frames are paced from the stall, not released as a burst.
    CHECK(pacer.Reserve(500'000'000) == 0);
    CHECK(pacer.Reserve(501'000'000) == 19'000'000);
    // Jitter within one period keeps the schedule instead of losing time.
    CHECK(pacer.Reserve(545'000'000) == 0);
    CHECK(pacer.Reserve(556'000'000) == 4'000'000);
}

TEST_CASE("frame pacer claims polled slots only when due") {
    ogplay::hal::FramePacer pacer(20'000'000);
    CHECK(pacer.TryClaim(0));
    CHECK_FALSE(pacer.TryClaim(5'000'000));
    CHECK_FALSE(pacer.TryClaim(19'999'999));
    CHECK(pacer.TryClaim(21'000'000));
    // A miss leaves the schedule alone: the next slot is still 40 ms.
    CHECK_FALSE(pacer.TryClaim(39'000'000));
    CHECK(pacer.TryClaim(40'000'000));
}

TEST_CASE("frame pacer without a cap never waits") {
    ogplay::hal::FramePacer pacer;
    CHECK(pacer.PeriodNs() == 0);
    CHECK(pacer.Reserve(0) == 0);
    CHECK(pacer.Reserve(1) == 0);
    CHECK(pacer.TryClaim(1));
    CHECK(pacer.TryClaim(1));
    ogplay::hal::SleepPreciseNs(0);
}
