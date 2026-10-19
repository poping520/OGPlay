#include <doctest/doctest.h>
#include "frontend/gui/dashboard.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "frontend/gui/launch.h"
#include "ogplay/app/library.h"

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

ogplay::app::LibraryEntry Entry(const std::filesystem::path& directory,
                                     const std::filesystem::path& external) {
    ogplay::app::LibraryEntry entry;
    entry.key = "org.example.game";
    entry.directory = directory;
    entry.metadata = ogplay::app::LibraryMetadata{
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

// The key editor port is picked per launch; compare everything else.
std::vector<std::string> WithoutEditorPort(std::vector<std::string> argv) {
    const auto it = std::find(argv.begin(), argv.end(), "--key-editor-port");
    if (it != argv.end() && std::next(it) != argv.end()) argv.erase(it, std::next(it, 2));
    return argv;
}

}  // namespace

TEST_CASE("GUI global launch settings forward only supported CLI controls") {
    using namespace ogplay::frontend;
    using namespace ogplay::app;
    TemporaryDirectory tree;
    const auto cli = tree.path / "ogplay";
    const auto directory = tree.path / "library" / "org.example.game";
    Write(cli, "exe"); Write(directory / "game.apk", "apk");
    auto entry = Entry(directory, {});
    entry.metadata->external_dir.reset();
    LibraryConfig config;
    const auto baseline = WithoutEditorPort(BuildLaunchPlan(cli, tree.path, entry, config).argv);
    SetSetting(config, "network_policy", std::string("allow"));
    SetSetting(config, "volume", std::uint32_t{25});
    CHECK(WithoutEditorPort(BuildLaunchPlan(cli, tree.path, entry, config).argv) == baseline);
    SetSetting(config, "supersample", std::uint32_t{2});
    SetSetting(config, "interpreter", std::string("threaded"));
    SetSetting(config, "mcp_enabled", true);
    SetSetting(config, "mcp_port", std::uint32_t{12345});
    const auto argv = BuildLaunchPlan(cli, tree.path, entry, config).argv;
    CHECK(argv.size() == baseline.size() + 6);
    CHECK(std::find(argv.begin(), argv.end(), "--key-editor-port") == argv.end());  // shares the MCP port
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
    using namespace ogplay::app;
    TemporaryDirectory tree;
    const auto cli = tree.path / "ogplay", directory = tree.path / "library" / "org.example.game", external = tree.path / "external";
    Write(cli, "exe"); Write(directory / "game.apk", "apk");
    std::filesystem::create_directories(external);
    auto entry = Entry(directory, {}); entry.metadata->external_dir.reset();
    LibraryConfig global;
    SetSetting(global, "supersample", std::uint32_t{4});
    SetSetting(global, "mcp_enabled", true);
    SetSetting(global, "dashboard_auto_open", true);
    GameSettings settings{{{"supersample", std::uint32_t{2}}, {"mcp_manual_step", true},
        {"ephemeral_sandbox", true}, {"external_dir", external.generic_string()}, {"model", std::string("reserved")}}};
    SaveGameSettings(directory, settings);
    const auto has = [](const auto& args, const auto& value) { return std::find(args.begin(), args.end(), value) != args.end(); };
    const auto normal_plan = BuildLaunchPlan(cli, tree.path, entry, global);
    CHECK(normal_plan.mcp_port == 15971);
    CHECK(normal_plan.dashboard_auto_open);
    CHECK_FALSE(BuildLaunchPlan(cli, tree.path, entry, global, LaunchMode::preflight).mcp_port);
    CHECK_FALSE(BuildLaunchPlan(cli, tree.path, entry, global, LaunchMode::preflight).dashboard_auto_open);
    const auto normal = normal_plan.argv;
    CHECK(has(normal, "2")); CHECK_FALSE(has(normal, "4")); CHECK_FALSE(has(normal, "reserved"));
    CHECK(has(normal, "--external-dir")); CHECK(has(normal, external.generic_string()));
    CHECK(has(normal, "--mcp-port")); CHECK(has(normal, "--mcp-manual-step"));
    CHECK(has(normal, "--ephemeral-sandbox")); CHECK_FALSE(has(normal, "--sandbox-dir"));
    const auto preflight = BuildLaunchPlan(cli, tree.path, entry, global, LaunchMode::preflight).argv;
    CHECK(has(preflight, "--preflight")); CHECK_FALSE(has(preflight, "--mcp-port")); CHECK_FALSE(has(preflight, "--mcp-manual-step"));
    CHECK(has(BuildLaunchPlan(cli, tree.path, entry, global, LaunchMode::diagnostic).argv, "--diag"));
    SetSetting(global, "mcp_enabled", false);
    CHECK_THROWS(static_cast<void>(BuildLaunchPlan(cli, tree.path, entry, global)));
    CHECK_NOTHROW(static_cast<void>(BuildLaunchPlan(cli, tree.path, entry, global, LaunchMode::preflight)));
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
    REQUIRE(plan.argv.size() == 21);
    CHECK(plan.argv[1] == "--run-game");
    CHECK(plan.argv[3] == "--profiles-dir");
    CHECK(plan.argv[5] == "--external-dir");
    CHECK(plan.argv[7] == "--cpu-cores");
    CHECK(plan.argv[8] == "1");
    CHECK(plan.argv[9] == "--cpu-frequency-mhz");
    CHECK(plan.argv[10] == "1000");
    CHECK(plan.argv[11] == "--key-editor-port");
    CHECK(plan.argv[12] == std::to_string(*plan.key_editor_port));
    CHECK(plan.argv[13] == "--sandbox-dir");
    CHECK(plan.argv[14] ==
          ogplay::app::LauncherSandboxRoot(temporary.path)
              .generic_string());
    CHECK(plan.argv[15] == "--library-root");
    CHECK(plan.argv[16] == std::filesystem::absolute(temporary.path).generic_string());
    CHECK(plan.argv[17] == "--installation-id");
    CHECK(plan.argv[18] == "org.example.game");
    CHECK(plan.argv[19] == "--input-config");
    CHECK(plan.package == "org.example.game");
    CHECK(plan.log_path == std::filesystem::absolute(entry_dir / "last-run.log"));
}

TEST_CASE("INP-5 GUI LaunchPlan always names the instance key layout file") {
    TemporaryDirectory temporary;
    const auto cli = temporary.path / "bin" / "ogplay";
    const auto entry_dir = temporary.path / "library" / "org.example.game";
    const auto external = temporary.path / "external";
    Write(cli, "exe"); Write(entry_dir / "game.apk", "apk");
    std::filesystem::create_directories(external);
    const auto plan = ogplay::frontend::BuildLaunchPlan(cli, temporary.path, Entry(entry_dir, external), {});
    REQUIRE(plan.argv.size() >= 2);
    CHECK(plan.argv[plan.argv.size() - 2] == "--input-config");
    CHECK(plan.argv.back() ==
          std::filesystem::absolute(entry_dir / "input.toml").lexically_normal().generic_string());
}

TEST_CASE("GUI LaunchPlan fails before spawn for every missing required input") {
    TemporaryDirectory temporary;
    const auto cli = temporary.path / "ogplay";
    const auto entry_dir = temporary.path / "entry";
    Write(cli, "exe"); Write(entry_dir / "game.apk", "apk");
    const auto entry = Entry(entry_dir, temporary.path / "missing-external");
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, temporary.path, entry, {})),
                    ogplay::app::LibraryError);
    auto damaged = entry;
    damaged.damage_reason = "broken";
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, temporary.path, damaged,
                        {})),
                    ogplay::app::LibraryError);

    auto valid = Entry(entry_dir, temporary.path / "external");
    std::filesystem::create_directories(*valid.metadata->external_dir);
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        temporary.path / "missing-cli", temporary.path, valid,
                        {})),
                    ogplay::app::LibraryError);
    std::filesystem::remove(entry_dir / "game.apk");
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, temporary.path, valid,
                        {})),
                    ogplay::app::LibraryError);
    Write(entry_dir / "game.apk", "apk");
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, temporary.path, valid,
                        {.profiles_dir = temporary.path / "missing-profiles"})),
                    ogplay::app::LibraryError);
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, {}, valid, {})),
                    ogplay::app::LibraryError);
}

TEST_CASE("BND-42 GUI persists and forwards bounded CPU configuration") {
    using namespace ogplay::frontend;
    using namespace ogplay::app;
    TemporaryDirectory tree;
    const auto cli = tree.path / "ogplay", directory = tree.path / "library" / "org.example.game";
    Write(cli, "exe"); Write(directory / "game.apk", "apk");
    auto entry = Entry(directory, {}); entry.metadata->external_dir.reset();
    GameSettings settings{{{"cpu_cores", std::uint32_t{4}}, {"cpu_frequency_mhz", std::uint32_t{1500}}}};
    SaveGameSettings(directory, settings);
    const auto plan = BuildLaunchPlan(cli, tree.path, entry, {});
    for (const auto& [flag, value] : {std::pair{"--cpu-cores", "4"}, std::pair{"--cpu-frequency-mhz", "1500"}}) {
        const auto found = std::find(plan.argv.begin(), plan.argv.end(), flag);
        REQUIRE(found != plan.argv.end());
        REQUIRE(std::next(found) != plan.argv.end());
        CHECK(*std::next(found) == value);
    }
    settings.values["cpu_cores"] = std::uint32_t{33};
    CHECK_THROWS(ValidateGameSettings(settings));
    settings.values["cpu_cores"] = std::uint32_t{1};
    settings.values["cpu_frequency_mhz"] = std::uint32_t{0};
    CHECK_THROWS(ValidateGameSettings(settings));
}

TEST_CASE("BND-43 GUI persists optional execution limits independently of nominal frequency") {
    using namespace ogplay::frontend;
    using namespace ogplay::app;
    TemporaryDirectory tree;
    const auto cli = tree.path / "ogplay", directory = tree.path / "library" / "org.example.game";
    Write(cli, "exe"); Write(directory / "game.apk", "apk");
    auto entry = Entry(directory, {}); entry.metadata->external_dir.reset();
    GameSettings settings{{{"cpu_limit_parallelism", true}, {"cpu_max_mticks_per_second", std::uint32_t{20}}}};
    SaveGameSettings(directory, settings);
    const auto plan = BuildLaunchPlan(cli, tree.path, entry, {});
    CHECK(std::find(plan.argv.begin(), plan.argv.end(), "--cpu-limit-parallelism") != plan.argv.end());
    const auto rate = std::find(plan.argv.begin(), plan.argv.end(), "--cpu-max-mticks-per-second");
    REQUIRE(rate != plan.argv.end()); REQUIRE(std::next(rate) != plan.argv.end());
    CHECK(*std::next(rate) == "20");
    settings.values["cpu_max_mticks_per_second"] = std::uint32_t{10001};
    CHECK_THROWS(ValidateGameSettings(settings));
    SaveGameSettings(directory, {});
    const auto defaults = BuildLaunchPlan(cli, tree.path, entry, {}).argv;
    CHECK(std::find(defaults.begin(), defaults.end(), "--cpu-limit-parallelism") == defaults.end());
    CHECK(std::find(defaults.begin(), defaults.end(), "--cpu-max-mticks-per-second") == defaults.end());
}

TEST_CASE("GUI launch tracker rejects duplicates and returns exact exits") {
    ogplay::frontend::LaunchTracker tracker;
    tracker.Begin("org.example.b", "b.log");
    tracker.Begin("org.example.a", "a.log");
    CHECK(tracker.IsRunning("org.example.a"));
    CHECK(tracker.RunningPackages() ==
          std::vector<std::string>{"org.example.a", "org.example.b"});
    CHECK_THROWS_AS(tracker.Begin("org.example.a", "again.log"),
                    ogplay::app::LibraryError);
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
    Write(log, "info boot\nfatal guest.liblog mono assertion\nerror guest.liblog Unity [EGL] Failed to create surface\n"
               "info APK session stopped presented=0\nerror guest exited before the first presented frame\n");
    const auto tail = ogplay::frontend::ReadLogTail(log);
    CHECK(tail.find("fatal guest.liblog mono assertion") != std::string::npos);
    CHECK(tail.find("Unity [EGL] Failed to create surface") != std::string::npos);
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

TEST_CASE("INP-7 GUI LaunchPlan gives every game a key editor port and a token outside argv") {
    using namespace ogplay::frontend;
    using namespace ogplay::app;
    TemporaryDirectory tree;
    const auto cli = tree.path / "ogplay";
    const auto directory = tree.path / "library" / "org.example.game";
    Write(cli, "exe"); Write(directory / "game.apk", "apk");
    auto entry = Entry(directory, {});
    entry.metadata->external_dir.reset();
    LibraryConfig config;
    const auto plan = BuildLaunchPlan(cli, tree.path, entry, config);
    REQUIRE(plan.key_editor_port.has_value());
    CHECK(*plan.key_editor_port != 0);
    CHECK(plan.control_token.size() == 32);
    CHECK(std::find(plan.argv.begin(), plan.argv.end(), plan.control_token) == plan.argv.end());
    CHECK(BuildLaunchPlan(cli, tree.path, entry, config).control_token != plan.control_token);
    SetSetting(config, "mcp_enabled", true);
    SetSetting(config, "mcp_port", std::uint32_t{12345});
    const auto with_mcp = BuildLaunchPlan(cli, tree.path, entry, config);
    CHECK(with_mcp.key_editor_port == std::optional<std::uint16_t>{std::uint16_t{12345}});
    const auto preflight = BuildLaunchPlan(cli, tree.path, entry, config, LaunchMode::preflight);
    CHECK_FALSE(preflight.key_editor_port);
    CHECK(preflight.control_token.empty());
    CHECK(std::find(preflight.argv.begin(), preflight.argv.end(), "--key-editor-port") == preflight.argv.end());
}

TEST_CASE("display user settings resolve game over global over defaults") {
    using namespace ogplay::frontend;
    using namespace ogplay::app;
    TemporaryDirectory tree;
    const auto executable = tree.path / "ogplay";
    const auto directory = tree.path / "library/org.example.game";
    Write(executable, "exe"); Write(directory / "game.apk", "apk");
    auto entry = Entry(directory, {}); entry.metadata->external_dir.reset();
    LibraryConfig global;
    auto launch = ResolveLaunch(LibraryStore(tree.path), entry, global);
    CHECK(launch.options.surface_width == 800);
    CHECK(launch.options.surface_height == 480);
    CHECK(launch.options.supersample_factor == 1);
    SetSetting(global, "surface_width", std::uint32_t{1280});
    SetSetting(global, "surface_height", std::uint32_t{720});
    SetSetting(global, "supersample", std::uint32_t{2});
    launch = ResolveLaunch(LibraryStore(tree.path), entry, global);
    CHECK(launch.options.surface_width == 1280);
    CHECK(launch.options.surface_height == 720);
    CHECK(launch.options.supersample_factor == 2);
    SaveGameSettings(directory, {{{"surface_width", std::uint32_t{1024}},
                                  {"surface_height", std::uint32_t{600}},
                                  {"supersample", std::uint32_t{3}}}});
    launch = ResolveLaunch(LibraryStore(tree.path), entry, global);
    CHECK(launch.options.surface_width == 1024);
    CHECK(launch.options.surface_height == 600);
    CHECK(launch.options.supersample_factor == 3);
    const auto argv = FormatGameRunArgs(launch.options);
    CHECK(std::find(argv.begin(), argv.end(), "--surface-width") != argv.end());
    CHECK(std::find(argv.begin(), argv.end(), "600") != argv.end());
    SaveGameSettings(directory, {});
    CHECK(ResolveLaunch(LibraryStore(tree.path), entry, global).options.surface_width == 1280);
    CHECK_THROWS(SetSetting(global, "surface_width", std::uint32_t{0}));
    CHECK_THROWS(SetSetting(global, "surface_height", std::uint32_t{4097}));
    CHECK_THROWS(ValidateGameSettings({{{"surface_width", std::uint32_t{0}}}}));
    CHECK_THROWS(ValidateGameSettings({{{"surface_height", std::uint32_t{4097}}}}));
}
