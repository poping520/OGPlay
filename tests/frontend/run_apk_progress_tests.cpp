#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

#include "ogplay/core/logger.h"
#include "ogplay/frontend/run_apk_progress.h"

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
