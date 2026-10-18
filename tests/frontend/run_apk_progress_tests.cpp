#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <thread>

#include "ogplay/core/logger.h"
#include "ogplay/frontend/run_apk_progress.h"

TEST_CASE("run-apk exit status preserves guest codes and rejects zero-present startup") {
    using namespace ogplay::frontend;
    ogplay::core::Logger logger;
    RunApkExitState state{.guest_exit = true, .frame = 12, .presented = 1};
    CHECK(ReportRunApkExit(logger, state) == 0); // Activity.finish.
    for (const auto code : {0, 29, 37}) {
        state.java_exit_code = code;
        CHECK(ReportRunApkExit(logger, state) == code);
        state.java_exit_code.reset();
        state.native_exit_code = code;
        CHECK(ReportRunApkExit(logger, state) == code);
        state.native_exit_code.reset();
    }
    state.termination_signal = 6;
    CHECK(ReportRunApkExit(logger, state) == 134);
    state.termination_signal = 15;
    CHECK(ReportRunApkExit(logger, state) == 143);
    state.termination_signal.reset();
    CHECK(logger.Snapshot().empty());
    state.presented = 0;
    CHECK(ReportRunApkExit(logger, state) == kRunApkStartupFailureExitCode);
    const auto failed = logger.Snapshot(ogplay::core::LogLevel::error, "frontend.run_apk");
    REQUIRE(failed.size() == 1);
    CHECK(failed.front().frame == 12);
    CHECK(failed.front().message == "guest exited before the first presented frame");
    CHECK(failed.front().fields.size() == 5);
    state.java_exit_code = 0;
    CHECK(ReportRunApkExit(logger, state) == kRunApkStartupFailureExitCode);
    state.java_exit_code = 29;
    CHECK(ReportRunApkExit(logger, state) == 29);
    state.native_exit_code = 37;
    CHECK(ReportRunApkExit(logger, state) == 37);
    state.termination_signal = 6;
    CHECK(ReportRunApkExit(logger, state) == 134);
    const auto count = logger.Snapshot().size();
    state.host_requested = true;
    CHECK(ReportRunApkExit(logger, state) == 0); // Window close, frame limit or MCP shutdown.
    CHECK(logger.Snapshot().size() == count);
    state = {};
    CHECK(ReportRunApkExit(logger, state) == 0); // Host ended without a guest exit.
}

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

TEST_CASE("host event thread gate rejects DexVM worker threads") {
    ogplay::frontend::HostEventThreadGate gate;
    CHECK(gate.IsOwnerThread());

    bool worker_is_owner = true;
    std::thread worker([&] { worker_is_owner = gate.IsOwnerThread(); });
    worker.join();

    CHECK_FALSE(worker_is_owner);
    CHECK(gate.IsOwnerThread());
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
    ogplay::frontend::LogProfileFrameProgress(logger, 600U, 600000U, 600U, 12U, 5U);
    const auto frames = logger.Snapshot(std::nullopt, "frontend.run_apk");
    REQUIRE(frames.size() == 2U);
    CHECK(frames.front().frame == 1U);
    CHECK(frames.front().level == ogplay::core::LogLevel::debug);
    CHECK(frames.back().frame == 600U);
    CHECK(frames.back().level == ogplay::core::LogLevel::debug);
}
