#include <doctest/doctest.h>
#include <cstdint>
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
    o.exit_after_frames = 120; o.mcp_port = std::uint16_t{15971}; o.mcp_manual_step = true;
    o.dexvm_interpreter = InterpreterBackend::threaded;
    o.diagnostics_enabled = true; o.diagnostic_directory = "/diag";
    o.diagnostic_teardown_timeout = std::chrono::seconds(7);
    o.surface_width = 1024; o.surface_height = 600;
    o.supersample_factor = 4; o.cpu_config = {8, 2400, {true, 123}};
    o.survey_gaps_output = "/survey.json"; o.sandbox_directory = "/sandbox";
    o.installation_id = "org.example.app-1"; o.input_config_path = "/input.toml";
    o.library_root = "/library"; o.missing_data_report = "/missing.json";
    CHECK(RoundTrip(o) == o);
    o.mcp_port.reset(); o.mcp_manual_step = false; o.key_editor_port = std::uint16_t{1234};
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
    o.cpu_config.cores = 1; o.mcp_port = std::uint16_t{0};
    CHECK_THROWS_AS(RunGame(o), std::invalid_argument);
    o.mcp_port.reset(); o.diagnostic_teardown_timeout = std::chrono::milliseconds(1);
    CHECK_THROWS_AS(static_cast<void>(FormatGameRunArgs(o)), std::invalid_argument);
}

#include <doctest/doctest.h>
#include "ogplay/app/game_run.h"

#include <array>
#include <stdexcept>
#include <string_view>

TEST_CASE("game run options parse without payload or entry-specific argv") {
    using namespace ogplay::app;
    const std::array<std::string_view, 1> args{"game.apk"};
    const auto options = ParseGameRunArgs(args);
    CHECK(options.apk_path == "game.apk");
    CHECK_FALSE(options.profiles_directory.has_value());
    CHECK_FALSE(options.mcp_port.has_value());
    CHECK(options.cpu_config.cores == 1);
    CHECK(options.cpu_config.frequency_mhz == 1000);
    CHECK_FALSE(options.ephemeral_sandbox);
    CHECK_THROWS_AS(static_cast<void>(ParseGameRunArgs({})), std::invalid_argument);
}

TEST_CASE("game run options preserve UTF-8 paths and shared runtime controls") {
    using namespace ogplay::app;
    const std::array<std::string_view, 13> args{
        "游戏 路径/game.apk", "--profiles-dir", "配方 目录", "--input-config", "按键.toml",
        "--mcp", "--mcp-manual-step", "--cpu-cores", "2", "--cpu-limit-parallelism",
        "--ephemeral-sandbox", "--dexvm-interpreter", "switch"};
    const auto options = ParseGameRunArgs(args);
    CHECK(options.apk_path.generic_u8string() == u8"游戏 路径/game.apk");
    REQUIRE(options.profiles_directory.has_value());
    CHECK(options.profiles_directory->generic_u8string() == u8"配方 目录");
    REQUIRE(options.input_config_path.has_value());
    CHECK(options.input_config_path->generic_u8string() == u8"按键.toml");
    CHECK(options.mcp_port == 15971);
    CHECK(options.mcp_manual_step);
    CHECK(options.cpu_config.cores == 2);
    CHECK(options.cpu_config.execution.limit_parallelism);
    CHECK(options.ephemeral_sandbox);
}

TEST_CASE("game run options reject incompatible execution modes before loading data") {
    using namespace ogplay::app;
    const std::array<std::string_view, 3> preflight{"game.apk", "--mcp", "--preflight"};
    CHECK_THROWS_AS(static_cast<void>(ParseGameRunArgs(preflight)), std::invalid_argument);
    const std::array<std::string_view, 2> manual{"game.apk", "--mcp-manual-step"};
    CHECK_THROWS_AS(static_cast<void>(ParseGameRunArgs(manual)), std::invalid_argument);
    const std::array<std::string_view, 5> sandbox{
        "game.apk", "--ephemeral-sandbox", "--sandbox-dir", "sandbox", "--preflight"};
    CHECK_THROWS_AS(static_cast<void>(ParseGameRunArgs(sandbox)), std::invalid_argument);
}

TEST_CASE("surface dimensions validate CLI and facade boundaries") {
    for (const auto flag : {"--surface-width", "--surface-height"}) {
        for (const auto value : {"0", "4097", "-1", "2x", "99999999999999999999999999"}) {
            const std::vector<std::string_view> args{"game.apk", flag, value};
            CHECK_THROWS_AS(static_cast<void>(ParseGameRunArgs(args)), std::invalid_argument);
        }
        const std::vector<std::string_view> args{"game.apk", flag, "4096"};
        CHECK_NOTHROW(static_cast<void>(ParseGameRunArgs(args)));
    }
    GameRunOptions options; options.apk_path = "game.apk";
    options.surface_width = 0;
    CHECK_THROWS_AS(ValidateGameRunOptions(options), std::invalid_argument);
    options.surface_width = 1024; options.surface_height = 4097;
    CHECK_THROWS_AS(ValidateGameRunOptions(options), std::invalid_argument);
}
