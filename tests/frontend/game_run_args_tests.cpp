#include <doctest/doctest.h>
#include "ogplay/app/game_run.h"
using namespace ogplay::app;
namespace {
GameRunOptions RoundTrip(const GameRunOptions& options) {
    auto args = FormatGameRunArgs(options);
    std::vector<std::string_view> views(args.begin(), args.end());
    return ParseGameRunArgs(views);
}
}
TEST_CASE("Game run args round trip every value and optional field") {
    GameRunOptions o;
    o.apk_path = std::filesystem::path(u8"/tmp/中文 空格/game.apk");
    CHECK(RoundTrip(o) == o);
    o.external_directory = "/data"; o.external_guest_directory = "/sdcard/data";
    o.obb_path = "/data/game.obb"; o.profiles_directory = "/profiles";
    o.exit_after_frames = 120; o.mcp_port = 15971; o.mcp_manual_step = true;
    o.dexvm_interpreter = InterpreterBackend::threaded;
    o.diagnostics_enabled = true; o.diagnostic_directory = "/diag";
    o.diagnostic_teardown_timeout = std::chrono::seconds(7);
    o.supersample_factor = 4; o.cpu_config = {8, 2400, {true, 123}};
    o.survey_gaps_output = "/survey.json"; o.sandbox_directory = "/sandbox";
    o.installation_id = "org.example.app-1"; o.input_config_path = "/input.toml";
    o.library_root = "/library";
    CHECK(RoundTrip(o) == o);
    o.mcp_port.reset(); o.mcp_manual_step = false; o.key_editor_port = 1234;
    o.sandbox_directory.reset(); o.ephemeral_sandbox = true;
    o.dexvm_interpreter = InterpreterBackend::switch_dispatch;
    CHECK(RoundTrip(o) == o);
    o.key_editor_port.reset(); o.preflight = true;
    CHECK(RoundTrip(o) == o);
}
TEST_CASE("Game run args validate direct facade values") {
    GameRunOptions o; o.apk_path = "game.apk";
    o.cpu_config.cores = 0;
    CHECK_THROWS_AS(static_cast<void>(FormatGameRunArgs(o)), std::invalid_argument);
    o.cpu_config.cores = 1; o.mcp_port = 0;
    CHECK_THROWS_AS(RunGame(o), std::invalid_argument);
    o.mcp_port.reset(); o.diagnostic_teardown_timeout = std::chrono::milliseconds(1);
    CHECK_THROWS_AS(static_cast<void>(FormatGameRunArgs(o)), std::invalid_argument);
}
