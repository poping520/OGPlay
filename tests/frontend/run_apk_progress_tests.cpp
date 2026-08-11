#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

#include "ogplay/core/logger.h"
#include "ogplay/frontend/run_apk_progress.h"

TEST_CASE("frame loop only idle-sleeps when no guest frame was presented") {
    static_assert(!ogplay::frontend::ShouldIdleSleepAfterFrameStep(true));
    static_assert(ogplay::frontend::ShouldIdleSleepAfterFrameStep(false));
    CHECK_FALSE(ogplay::frontend::ShouldIdleSleepAfterFrameStep(true));
    CHECK(ogplay::frontend::ShouldIdleSleepAfterFrameStep(false));
}

TEST_CASE("host event pump gate throttles pumps to the host interval") {
    ogplay::frontend::HostEventPumpGate gate{4'000'000U};

    CHECK(gate.ShouldPump(0U));
    CHECK_FALSE(gate.ShouldPump(1U));
    CHECK_FALSE(gate.ShouldPump(3'999'999U));
    CHECK(gate.ShouldPump(4'000'000U));
    CHECK_FALSE(gate.ShouldPump(7'999'999U));
    CHECK(gate.ShouldPump(9'000'000U));
    CHECK_FALSE(gate.ShouldPump(12'999'999U));
    CHECK(gate.ShouldPump(13'000'000U));
}

TEST_CASE("host event pump gate with a zero interval always pumps") {
    ogplay::frontend::HostEventPumpGate gate{0U};

    CHECK(gate.ShouldPump(5U));
    CHECK(gate.ShouldPump(5U));
    CHECK(gate.ShouldPump(6U));
}

TEST_CASE("run-apk progress logging is bounded and structured") {
    ogplay::core::Logger logger;
    ogplay::frontend::RunApkGuestCallProgress call_progress{logger};

    call_progress.Begin(7U, 0x12345678U);
    call_progress.Observe(999999999U);
    CHECK(logger.Snapshot().empty());
    call_progress.Observe(1000000000U);
    call_progress.Observe(1500000000U);
    call_progress.Observe(2000000000U);
    call_progress.Complete(2100000000U);

    const auto calls = logger.Snapshot(std::nullopt, "runtime.guest_call");
    REQUIRE(calls.size() == 3U);
    CHECK(calls.front().frame == 7U);
    CHECK(calls.front().guest_ticks == 1000000000U);
    CHECK(calls.front().fields.size() == 2U);
    CHECK(calls.back().message == "long guest call completed");

    ogplay::frontend::LogProfileFrameProgress(logger, 1U, 1000U, 1U, 3U, 2U);
    ogplay::frontend::LogProfileFrameProgress(logger, 2U, 2000U, 2U, 4U, 3U);
    ogplay::frontend::LogProfileFrameProgress(logger, 60U, 60000U, 60U, 9U, 4U);
    const auto frames = logger.Snapshot(std::nullopt, "frontend.run_apk");
    REQUIRE(frames.size() == 2U);
    CHECK(frames.front().frame == 1U);
    CHECK(frames.back().frame == 60U);
}
