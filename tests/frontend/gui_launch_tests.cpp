#include <doctest/doctest.h>
#include "ogplay/frontend/gui_dashboard.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "ogplay/frontend/gui_launch.h"
#include "ogplay/frontend/gui_settings.h"

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{};
        path = std::filesystem::temp_directory_path() /
               ("ogplay-gui-launch-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        static_cast<void>(std::filesystem::remove_all(path, error));
    }
    std::filesystem::path path;
};

void Write(const std::filesystem::path& path, const std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    REQUIRE(output.good());
}

ogplay::frontend::LibraryEntry Entry(const std::filesystem::path& directory,
                                     const std::filesystem::path& external) {
    ogplay::frontend::LibraryEntry entry;
    entry.key = "org.example.game";
    entry.directory = directory;
    entry.metadata = ogplay::frontend::LibraryMetadata{
        .package = entry.key,
        .display_name = "Game",
        .version_code = 7,
        .version_name = "1",
        .imported_at = "now",
        .profile_id = "org.example.game",
        .external_dir = external,
    };
    return entry;
}

}  // namespace

TEST_CASE("GUI global launch settings forward only supported CLI controls") {
    using namespace ogplay::frontend;
    TemporaryDirectory tree;
    const auto cli = tree.path / "ogplay";
    const auto directory = tree.path / "entry";
    Write(cli, "exe"); Write(directory / "game.apk", "apk");
    auto entry = Entry(directory, {});
    entry.metadata->external_dir.reset();
    GuiConfig config;
    const auto baseline = BuildLaunchPlan(cli, tree.path, entry, config).argv;
    SetGuiSetting(config, "network_policy", std::string("allow"));
    SetGuiSetting(config, "volume", std::uint32_t{25});
    CHECK(BuildLaunchPlan(cli, tree.path, entry, config).argv == baseline);
    SetGuiSetting(config, "supersample", std::uint32_t{2});
    SetGuiSetting(config, "interpreter", std::string("threaded"));
    SetGuiSetting(config, "mcp_enabled", true);
    SetGuiSetting(config, "mcp_port", std::uint32_t{12345});
    const auto argv = BuildLaunchPlan(cli, tree.path, entry, config).argv;
    CHECK(argv.size() == baseline.size() + 6);
    for (const auto& [flag, value] : std::vector<std::pair<std::string, std::string>>{
        {"--supersample", "2"}, {"--dexvm-interpreter", "threaded"}, {"--mcp-port", "12345"}}) {
        const auto it = std::find(argv.begin(), argv.end(), flag);
        REQUIRE(it != argv.end());
        REQUIRE(std::next(it) != argv.end());
        CHECK(*std::next(it) == value);
    }
}

TEST_CASE("GUI instance launch overrides inherit and emit valid CLI mode combinations") {
    using namespace ogplay::frontend;
    TemporaryDirectory tree;
    const auto cli = tree.path / "ogplay", directory = tree.path / "entry", external = tree.path / "external";
    Write(cli, "exe"); Write(directory / "game.apk", "apk");
    std::filesystem::create_directories(external);
    auto entry = Entry(directory, {}); entry.metadata->external_dir.reset();
    GuiConfig global;
    SetGuiSetting(global, "supersample", std::uint32_t{4});
    SetGuiSetting(global, "mcp_enabled", true);
    SetGuiSetting(global, "dashboard_auto_open", true);
    GameSettings settings{{{"supersample", std::uint32_t{2}}, {"mcp_manual_step", true},
        {"ephemeral_sandbox", true}, {"external_dir", external.generic_string()}, {"model", std::string("reserved")}}};
    SaveGameSettings(directory, settings);
    const auto has = [](const auto& args, const auto& value) { return std::find(args.begin(), args.end(), value) != args.end(); };
    const auto normal_plan = BuildLaunchPlan(cli, tree.path, entry, global);
    CHECK(normal_plan.mcp_port == 15971);
    CHECK(normal_plan.dashboard_auto_open);
    CHECK_FALSE(BuildLaunchPlan(cli, tree.path, entry, global, GuiLaunchMode::preflight).mcp_port);
    CHECK_FALSE(BuildLaunchPlan(cli, tree.path, entry, global, GuiLaunchMode::preflight).dashboard_auto_open);
    const auto normal = normal_plan.argv;
    CHECK(has(normal, "2")); CHECK_FALSE(has(normal, "4")); CHECK_FALSE(has(normal, "reserved"));
    CHECK(has(normal, "--external-dir")); CHECK(has(normal, external.generic_string()));
    CHECK(has(normal, "--mcp-port")); CHECK(has(normal, "--mcp-manual-step"));
    CHECK(has(normal, "--ephemeral-sandbox")); CHECK_FALSE(has(normal, "--sandbox-dir"));
    const auto preflight = BuildLaunchPlan(cli, tree.path, entry, global, GuiLaunchMode::preflight).argv;
    CHECK(has(preflight, "--preflight")); CHECK_FALSE(has(preflight, "--mcp-port")); CHECK_FALSE(has(preflight, "--mcp-manual-step"));
    CHECK(has(BuildLaunchPlan(cli, tree.path, entry, global, GuiLaunchMode::diagnostic).argv, "--diag"));
    SetGuiSetting(global, "mcp_enabled", false);
    CHECK_THROWS(static_cast<void>(BuildLaunchPlan(cli, tree.path, entry, global)));
    CHECK_NOTHROW(static_cast<void>(BuildLaunchPlan(cli, tree.path, entry, global, GuiLaunchMode::preflight)));
    SaveGameSettings(directory, {});
    const auto inherited = BuildLaunchPlan(cli, tree.path, entry, global).argv;
    CHECK(has(inherited, "4")); CHECK(has(inherited, "--sandbox-dir")); CHECK_FALSE(has(inherited, "--external-dir"));
    entry.metadata->external_dir = external;
    SaveGameSettings(directory, {{{"external_dir", std::string()}}});
    CHECK_FALSE(has(BuildLaunchPlan(cli, tree.path, entry, global).argv, "--external-dir"));
}

TEST_CASE("GUI LaunchPlan emits only the documented run-apk arguments") {
    TemporaryDirectory temporary;
    const auto cli = temporary.path / "bin" / "ogplay";
    const auto entry_dir = temporary.path / "library" / "org.example.game";
    const auto profiles = temporary.path / "profiles";
    const auto external = temporary.path / "external";
    Write(cli, "exe"); Write(entry_dir / "game.apk", "apk");
    std::filesystem::create_directories(profiles);
    std::filesystem::create_directories(external);
    const auto plan = ogplay::frontend::BuildLaunchPlan(
        cli, temporary.path, Entry(entry_dir, external),
        {.profiles_dir = profiles});
    REQUIRE(plan.argv.size() == 11);
    CHECK(plan.argv[1] == "run-apk");
    CHECK(plan.argv[3] == "--profiles-dir");
    CHECK(plan.argv[5] == "--external-dir");
    CHECK(plan.argv[7] == "--sandbox-dir");
    CHECK(plan.argv[8] ==
          ogplay::frontend::LauncherSandboxRoot(temporary.path)
              .generic_string());
    CHECK(plan.argv[9] == "--installation-id");
    CHECK(plan.argv[10] == "org.example.game");
    CHECK(plan.package == "org.example.game");
    CHECK(plan.log_path == std::filesystem::absolute(entry_dir / "last-run.log"));
}

TEST_CASE("GUI LaunchPlan fails before spawn for every missing required input") {
    TemporaryDirectory temporary;
    const auto cli = temporary.path / "ogplay";
    const auto entry_dir = temporary.path / "entry";
    Write(cli, "exe"); Write(entry_dir / "game.apk", "apk");
    const auto entry = Entry(entry_dir, temporary.path / "missing-external");
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, temporary.path, entry, {})),
                    ogplay::frontend::GuiModelError);
    auto damaged = entry;
    damaged.damage_reason = "broken";
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, temporary.path, damaged,
                        {})),
                    ogplay::frontend::GuiModelError);

    auto valid = Entry(entry_dir, temporary.path / "external");
    std::filesystem::create_directories(*valid.metadata->external_dir);
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        temporary.path / "missing-cli", temporary.path, valid,
                        {})),
                    ogplay::frontend::GuiModelError);
    std::filesystem::remove(entry_dir / "game.apk");
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, temporary.path, valid,
                        {})),
                    ogplay::frontend::GuiModelError);
    Write(entry_dir / "game.apk", "apk");
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, temporary.path, valid,
                        {.profiles_dir = temporary.path / "missing-profiles"})),
                    ogplay::frontend::GuiModelError);
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, {}, valid, {})),
                    ogplay::frontend::GuiModelError);
}

TEST_CASE("GUI launch tracker rejects duplicates and returns exact exits") {
    ogplay::frontend::LaunchTracker tracker;
    tracker.Begin("org.example.b", "b.log");
    tracker.Begin("org.example.a", "a.log");
    CHECK(tracker.IsRunning("org.example.a"));
    CHECK(tracker.RunningPackages() ==
          std::vector<std::string>{"org.example.a", "org.example.b"});
    CHECK_THROWS_AS(tracker.Begin("org.example.a", "again.log"),
                    ogplay::frontend::GuiModelError);
    const auto result = tracker.Finish("org.example.a", 17);
    CHECK(result.package == "org.example.a");
    CHECK(result.exit_code == 17);
    CHECK(result.log_path == "a.log");
    CHECK_FALSE(tracker.IsRunning("org.example.a"));
}

TEST_CASE("GUI launch log tail is bounded by lines and bytes") {
    TemporaryDirectory temporary;
    const auto log = temporary.path / "last-run.log";
    Write(log, "one\ntwo\nthree\nfour\n");
    CHECK(ogplay::frontend::ReadLogTail(log, 2, 1024) == "three\nfour\n");
    CHECK(ogplay::frontend::ReadLogTail(log, 20, 5) == "four\n");
    Write(log, "prefix\n中文\n");
    CHECK(ogplay::frontend::ReadLogTail(log, 20, 5) == "文\n");
    CHECK(ogplay::frontend::ReadLogTail(temporary.path / "missing") == "");
}

TEST_CASE("GUI Dashboard probe rejects foreign process and malformed protocol") {
    using ogplay::frontend::DecodeDashboardProbe;
    const std::string good = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
    const std::string body = R"({"jsonrpc":"2.0","id":1,"result":{"schema_version":1,"process_id":123}})";
    CHECK(DecodeDashboardProbe(good + body, 123).ready);
    CHECK_FALSE(DecodeDashboardProbe(good + body, 124).ready);
    CHECK_FALSE(DecodeDashboardProbe(good + body, 0).ready);
    CHECK_FALSE(DecodeDashboardProbe("HTTP/1.1 404 Not Found\r\n\r\n" + body, 123).ready);
    CHECK_FALSE(DecodeDashboardProbe(good + "{}", 123).ready);
    CHECK_FALSE(DecodeDashboardProbe(good + "{", 123).ready);
    CHECK_FALSE(DecodeDashboardProbe(good + body + std::string(8192, ' '), 123).ready);
    CHECK_FALSE(DecodeDashboardProbe(good + R"({"id":1,"result":{"schema_version":1,"process_id":123}})", 123).ready);
    CHECK_FALSE(DecodeDashboardProbe(good + R"({"jsonrpc":"2.0","id":1,"result":{"schema_version":2,"process_id":123}})", 123).ready);
}
