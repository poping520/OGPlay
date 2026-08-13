#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "ogplay/frontend/gui_launch.h"

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

TEST_CASE("GUI LaunchPlan emits only the documented run-apk arguments") {
    TemporaryDirectory temporary;
    const auto cli = temporary.path / "bin" / "ogplay";
    const auto entry_dir = temporary.path / "library" / "org.example.game";
    const auto system = temporary.path / "system";
    const auto profiles = temporary.path / "profiles";
    const auto external = temporary.path / "external";
    Write(cli, "exe"); Write(entry_dir / "game.apk", "apk");
    std::filesystem::create_directories(system);
    std::filesystem::create_directories(profiles);
    std::filesystem::create_directories(external);
    const auto plan = ogplay::frontend::BuildLaunchPlan(
        cli, Entry(entry_dir, external), {system, profiles});
    REQUIRE(plan.argv.size() == 9);
    CHECK(plan.argv[1] == "run-apk");
    CHECK(plan.argv[3] == "--system-dir");
    CHECK(plan.argv[5] == "--profiles-dir");
    CHECK(plan.argv[7] == "--external-dir");
    CHECK(plan.package == "org.example.game");
    CHECK(plan.log_path == std::filesystem::absolute(entry_dir / "last-run.log"));
}

TEST_CASE("GUI LaunchPlan fails before spawn for every missing required input") {
    TemporaryDirectory temporary;
    const auto cli = temporary.path / "ogplay";
    const auto entry_dir = temporary.path / "entry";
    const auto system = temporary.path / "system";
    Write(cli, "exe"); Write(entry_dir / "game.apk", "apk");
    std::filesystem::create_directories(system);
    const auto entry = Entry(entry_dir, temporary.path / "missing-external");
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, entry, {system, std::nullopt})),
                    ogplay::frontend::GuiModelError);
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, entry, {})),
                    ogplay::frontend::GuiModelError);
    auto damaged = entry;
    damaged.damage_reason = "broken";
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, damaged, {system, std::nullopt})),
                    ogplay::frontend::GuiModelError);

    auto valid = Entry(entry_dir, temporary.path / "external");
    std::filesystem::create_directories(*valid.metadata->external_dir);
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        temporary.path / "missing-cli", valid,
                        {system, std::nullopt})),
                    ogplay::frontend::GuiModelError);
    std::filesystem::remove(entry_dir / "game.apk");
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, valid, {system, std::nullopt})),
                    ogplay::frontend::GuiModelError);
    Write(entry_dir / "game.apk", "apk");
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLaunchPlan(
                        cli, valid,
                        {system, temporary.path / "missing-profiles"})),
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
    CHECK(ogplay::frontend::ReadLogTail(temporary.path / "missing") == "");
}
