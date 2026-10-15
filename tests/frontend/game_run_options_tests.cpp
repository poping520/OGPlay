#include <doctest/doctest.h>
#include "frontend/game/run_game.h"

#include <array>
#include <stdexcept>
#include <string_view>

TEST_CASE("game run options parse without payload or entry-specific argv") {
    using namespace ogplay::frontend;
    const std::array<std::string_view, 1> args{"game.apk"};
    const auto options = ParseGameRunOptions(args);
    CHECK(options.apk_path == "game.apk");
    CHECK_FALSE(options.profiles_directory.has_value());
    CHECK_FALSE(options.mcp_port.has_value());
    CHECK(options.cpu_config.cores == 1);
    CHECK(options.cpu_config.frequency_mhz == 1000);
    CHECK_FALSE(options.ephemeral_sandbox);
    CHECK_THROWS_AS(static_cast<void>(ParseGameRunOptions({})), std::invalid_argument);
}

TEST_CASE("game run options preserve UTF-8 paths and shared runtime controls") {
    using namespace ogplay::frontend;
    const std::array<std::string_view, 13> args{
        "游戏 路径/game.apk", "--profiles-dir", "配方 目录", "--input-config", "按键.toml",
        "--mcp", "--mcp-manual-step", "--cpu-cores", "2", "--cpu-limit-parallelism",
        "--ephemeral-sandbox", "--dexvm-interpreter", "switch"};
    const auto options = ParseGameRunOptions(args);
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
    using namespace ogplay::frontend;
    const std::array<std::string_view, 3> preflight{"game.apk", "--mcp", "--preflight"};
    CHECK_THROWS_AS(static_cast<void>(ParseGameRunOptions(preflight)), std::invalid_argument);
    const std::array<std::string_view, 2> manual{"game.apk", "--mcp-manual-step"};
    CHECK_THROWS_AS(static_cast<void>(ParseGameRunOptions(manual)), std::invalid_argument);
    const std::array<std::string_view, 5> sandbox{
        "game.apk", "--ephemeral-sandbox", "--sandbox-dir", "sandbox", "--preflight"};
    CHECK_THROWS_AS(static_cast<void>(ParseGameRunOptions(sandbox)), std::invalid_argument);
}
