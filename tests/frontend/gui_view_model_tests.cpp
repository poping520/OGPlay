#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/frontend/gui_view_model.h"

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{};
        path = std::filesystem::temp_directory_path() /
               ("ogplay-gui-view-" + std::to_string(
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

ogplay::frontend::LibraryEntry Entry(
    std::string package, std::string name,
    std::optional<std::string> profile = "profile") {
    ogplay::frontend::LibraryEntry entry;
    entry.key = std::move(package);
    entry.metadata = ogplay::frontend::LibraryMetadata{
        .package = entry.key,
        .display_name = std::move(name),
        .version_code = 1,
        .version_name = "1",
        .imported_at = "now",
        .profile_id = std::move(profile),
    };
    return entry;
}

}  // namespace

TEST_CASE("GUI messages wait for workflows and preserve FIFO order") {
    ogplay::frontend::GuiMessageQueue messages;
    messages.Push("first", "first body");
    messages.Push("second", "second body");

    CHECK_FALSE(messages.ActivateNext(true));
    CHECK(messages.Active() == nullptr);
    CHECK(messages.ActivateNext(false));
    REQUIRE(messages.Active() != nullptr);
    CHECK(messages.Active()->title == "first");
    CHECK_FALSE(messages.ActivateNext(false));

    messages.DismissActive();
    CHECK(messages.ActivateNext(false));
    REQUIRE(messages.Active() != nullptr);
    CHECK(messages.Active()->title == "second");
    CHECK_THROWS_AS(messages.Push("", "body"), std::invalid_argument);
}

TEST_CASE("library tiles sort by display name and preserve cached icons") {
    auto second = Entry("org.example.second", "Zulu");
    auto first = Entry("org.example.first", "Alpha");
    first.icon_png = {std::byte{1}, std::byte{2}};
    const std::vector<ogplay::frontend::LibraryEntry> entries{
        std::move(second), std::move(first)};
    const auto tiles = ogplay::frontend::BuildLibraryTiles(entries, {});
    REQUIRE(tiles.size() == 2);
    CHECK(tiles[0].display_name == "Alpha");
    CHECK(tiles[0].icon_png == std::vector<std::byte>{std::byte{1}, std::byte{2}});
    CHECK(tiles[1].display_name == "Zulu");
}

TEST_CASE("library tile status follows the documented strict priority") {
    TemporaryDirectory temporary;
    const auto available = temporary.path / "external";
    std::filesystem::create_directories(available);

    auto damaged = Entry("org.example.damaged", "Damaged");
    damaged.metadata.reset();
    damaged.damage_reason = "bad metadata";
    auto no_profile = Entry("org.example.unsupported", "Unsupported", std::nullopt);
    auto no_external = Entry("org.example.no_data", "No data");
    auto running = Entry("org.example.running", "Running");
    running.metadata->external_dir = available;
    auto ready = Entry("org.example.ready", "Ready");
    const std::vector<ogplay::frontend::LibraryEntry> entries{
        damaged, no_profile, no_external, running, ready};
    const ogplay::frontend::LibraryViewContext context{
        .running_packages = {damaged.key, no_profile.key, no_external.key, running.key},
        .external_required_packages = {no_external.key, running.key},
    };
    const auto tiles = ogplay::frontend::BuildLibraryTiles(entries, context);
    const auto status = [&tiles](const std::string& key) {
        for (const auto& tile : tiles) {
            if (tile.key == key) return tile.status;
        }
        return ogplay::frontend::LibraryTileStatus::ready;
    };
    CHECK(status(damaged.key) == ogplay::frontend::LibraryTileStatus::damaged);
    CHECK(status(no_profile.key) ==
          ogplay::frontend::LibraryTileStatus::missing_profile);
    CHECK(status(no_external.key) ==
          ogplay::frontend::LibraryTileStatus::missing_external);
    CHECK(status(running.key) == ogplay::frontend::LibraryTileStatus::running);
    CHECK(status(ready.key) == ogplay::frontend::LibraryTileStatus::ready);
    const auto detail = [&tiles](const std::string& key) -> std::string {
        for (const auto& tile : tiles) {
            if (tile.key == key) return tile.detail;
        }
        return {};
    };
    CHECK(detail(no_profile.key).find("通用启动路径") != std::string::npos);
    CHECK(detail(no_external.key).find("数据包") != std::string::npos);
    CHECK(detail(running.key).find("先退出游戏") != std::string::npos);
    const auto no_profile_tile = std::find_if(
        tiles.begin(), tiles.end(), [&no_profile](const auto& tile) {
            return tile.key == no_profile.key;
        });
    REQUIRE(no_profile_tile != tiles.end());
    CHECK_FALSE(no_profile_tile->can_launch);

    auto idle_generic = Entry("org.example.generic", "Generic", std::nullopt);
    const auto generic_tiles = ogplay::frontend::BuildLibraryTiles(
        std::span<const ogplay::frontend::LibraryEntry>(&idle_generic, 1), {});
    REQUIRE(generic_tiles.size() == 1);
    CHECK(generic_tiles.front().status ==
          ogplay::frontend::LibraryTileStatus::missing_profile);
    CHECK(generic_tiles.front().can_launch);
}

TEST_CASE("unavailable Profile catalog fails closed before launch readiness") {
    auto damaged = Entry("org.example.damaged", "Damaged");
    damaged.metadata.reset();
    damaged.damage_reason = "bad metadata";
    const auto ready = Entry("org.example.ready", "Ready");
    const std::vector<ogplay::frontend::LibraryEntry> entries{damaged, ready};
    const auto tiles = ogplay::frontend::BuildLibraryTiles(
        entries, {.running_packages = {ready.key},
                  .external_required_packages = {},
                  .profile_catalog_error = "catalog fixture unavailable"});
    REQUIRE(tiles.size() == 2);
    const auto find = [&tiles](const std::string& key)
        -> const ogplay::frontend::LibraryTile* {
        for (const auto& tile : tiles) {
            if (tile.key == key) return &tile;
        }
        return nullptr;
    };
    const auto* damaged_tile = find(damaged.key);
    const auto* ready_tile = find(ready.key);
    REQUIRE(damaged_tile != nullptr);
    REQUIRE(ready_tile != nullptr);
    CHECK(damaged_tile->status == ogplay::frontend::LibraryTileStatus::damaged);
    CHECK(ready_tile->status ==
          ogplay::frontend::LibraryTileStatus::profile_catalog_unavailable);
    CHECK(ready_tile->detail.find("catalog fixture unavailable") !=
          std::string::npos);
    CHECK(ready_tile->detail.find("设置") != std::string::npos);
}

TEST_CASE("system setup is visible after title requirements and running state") {
    TemporaryDirectory temporary;
    const auto external = temporary.path / "external";
    std::filesystem::create_directories(external);
    auto missing_external = Entry("org.example.data", "Data");
    auto running = Entry("org.example.running", "Running");
    auto setup = Entry("org.example.setup", "Setup");
    running.metadata->external_dir = external;
    const std::vector<ogplay::frontend::LibraryEntry> entries{
        missing_external, running, setup};
    const auto tiles = ogplay::frontend::BuildLibraryTiles(
        entries,
        {.running_packages = {running.key, missing_external.key},
         .external_required_packages = {missing_external.key, running.key},
         .system_directory_error = "system fixture unavailable"});
    const auto status = [&tiles](const std::string& key) {
        for (const auto& tile : tiles) {
            if (tile.key == key) return tile.status;
        }
        return ogplay::frontend::LibraryTileStatus::ready;
    };
    CHECK(status(missing_external.key) ==
          ogplay::frontend::LibraryTileStatus::missing_external);
    CHECK(status(running.key) ==
          ogplay::frontend::LibraryTileStatus::running);
    CHECK(status(setup.key) ==
          ogplay::frontend::LibraryTileStatus::setup_required);
    const auto missing_detail = ogplay::frontend::BuildLibraryDetail(
        missing_external, *std::find_if(tiles.begin(), tiles.end(),
                                       [&missing_external](const auto& tile) {
                                           return tile.key == missing_external.key;
                                       }),
        {.running_packages = {missing_external.key},
         .external_required_packages = {missing_external.key},
         .system_directory_error = "system fixture unavailable"});
    CHECK_FALSE(missing_detail.can_delete);

    auto generic = Entry("org.example.generic", "Generic", std::nullopt);
    const auto generic_tiles = ogplay::frontend::BuildLibraryTiles(
        std::span<const ogplay::frontend::LibraryEntry>(&generic, 1),
        {.system_directory_error = "system fixture unavailable"});
    REQUIRE(generic_tiles.size() == 1);
    CHECK(generic_tiles.front().status ==
          ogplay::frontend::LibraryTileStatus::missing_profile);
    CHECK_FALSE(generic_tiles.front().can_launch);
}

TEST_CASE("library selection preserves packages and falls back beside removals") {
    const std::vector<ogplay::frontend::LibraryEntry> entries{
        Entry("org.example.alpha", "Alpha"),
        Entry("org.example.bravo", "Bravo"),
        Entry("org.example.charlie", "Charlie")};
    auto tiles = ogplay::frontend::BuildLibraryTiles(entries, {});
    ogplay::frontend::LibrarySelection selection;
    selection.Reconcile(tiles);
    REQUIRE(selection.Key().has_value());
    CHECK(*selection.Key() == "org.example.alpha");

    selection.Select("org.example.bravo", tiles);
    selection.Reconcile(tiles);
    CHECK(*selection.Key() == "org.example.bravo");

    tiles.erase(tiles.begin() + 1);
    selection.Reconcile(tiles);
    CHECK(*selection.Key() == "org.example.charlie");

    tiles.clear();
    selection.Reconcile(tiles);
    CHECK_FALSE(selection.Key().has_value());
}

TEST_CASE("library detail exposes factual conditions without inferring runtime facts") {
    TemporaryDirectory temporary;
    const auto external = temporary.path / "external";
    const auto system = temporary.path / "system";
    std::filesystem::create_directories(external);
    std::filesystem::create_directories(system);

    auto entry = Entry("org.example.game", "Example");
    entry.metadata->version_code = 42;
    entry.metadata->version_name.clear();
    entry.metadata->profile_id = "exact.profile";
    entry.metadata->external_dir = external;
    const ogplay::frontend::LibraryViewContext context{
        .external_required_packages = {entry.key},
    };
    const auto tiles = ogplay::frontend::BuildLibraryTiles(
        std::span<const ogplay::frontend::LibraryEntry>(&entry, 1), context);
    REQUIRE(tiles.size() == 1);
    const auto detail =
        ogplay::frontend::BuildLibraryDetail(entry, tiles.front(), context);
    CHECK(detail.package == entry.key);
    CHECK(detail.version == "versionCode 42");
    CHECK(detail.profile.status ==
          ogplay::frontend::LibraryConditionStatus::ready);
    CHECK(detail.profile.value == "exact.profile");
    CHECK(detail.external.status ==
          ogplay::frontend::LibraryConditionStatus::ready);
    CHECK(detail.external.value == "已就绪");
    CHECK(detail.system.status ==
          ogplay::frontend::LibraryConditionStatus::ready);
    CHECK(detail.can_launch);
    CHECK(detail.can_delete);
    CHECK_FALSE(detail.version.find("ARM") != std::string::npos);
    CHECK_FALSE(detail.version.find("API") != std::string::npos);
}

TEST_CASE("library detail distinguishes external and damaged states") {
    auto optional = Entry("org.example.optional", "Optional");
    auto missing = Entry("org.example.missing", "Missing");
    auto unknown = Entry("org.example.unknown", "Unknown", std::nullopt);
    auto damaged = Entry("org.example.damaged", "Damaged");
    damaged.metadata.reset();
    damaged.damage_reason = "broken fixture";

    const ogplay::frontend::LibraryViewContext context{
        .external_required_packages = {missing.key},
        .system_directory_error = "system missing",
    };
    const std::vector<ogplay::frontend::LibraryEntry> entries{
        optional, missing, unknown, damaged};
    const auto tiles = ogplay::frontend::BuildLibraryTiles(entries, context);
    const auto detail = [&entries, &tiles, &context](const std::string& key) {
        const auto entry = std::find_if(entries.begin(), entries.end(),
                                        [&key](const auto& value) {
                                            return value.key == key;
                                        });
        const auto tile = std::find_if(tiles.begin(), tiles.end(),
                                       [&key](const auto& value) {
                                           return value.key == key;
                                       });
        REQUIRE(entry != entries.end());
        REQUIRE(tile != tiles.end());
        return ogplay::frontend::BuildLibraryDetail(*entry, *tile, context);
    };
    CHECK(detail(optional.key).external.status ==
          ogplay::frontend::LibraryConditionStatus::not_required);
    CHECK(detail(missing.key).external.status ==
          ogplay::frontend::LibraryConditionStatus::missing);
    CHECK(detail(unknown.key).external.status ==
          ogplay::frontend::LibraryConditionStatus::unavailable);
    const auto damaged_detail = detail(damaged.key);
    CHECK(damaged_detail.external.status ==
          ogplay::frontend::LibraryConditionStatus::unavailable);
    CHECK_FALSE(damaged_detail.can_launch);
    CHECK(damaged_detail.can_delete);
}

TEST_CASE("system directory readiness distinguishes unset invalid and ready") {
    TemporaryDirectory temporary;
    const auto ready = temporary.path / "system";
    std::filesystem::create_directories(ready);
    CHECK(ogplay::frontend::GuiSystemDirectoryError({}).has_value());
    CHECK(ogplay::frontend::GuiSystemDirectoryError(
              {.system_dir = temporary.path / "missing"})
              .has_value());
    CHECK_FALSE(ogplay::frontend::GuiSystemDirectoryError(
                    {.system_dir = ready})
                    .has_value());
}

TEST_CASE("CJK font selection preserves candidates and supports ASCII fallback") {
    TemporaryDirectory temporary;
    const auto missing = temporary.path / "missing.ttf";
    const auto available = temporary.path / "font.ttc";
    std::ofstream(available, std::ios::binary).put('x');
    const std::vector<std::filesystem::path> candidates{missing, available};
    CHECK(ogplay::frontend::SelectCjkFont(candidates) == available);
    CHECK(ogplay::frontend::SelectCjkFont(
              std::span<const std::filesystem::path>(&missing, 1)).empty());
}

TEST_CASE("GUI event wait keeps smoke bounded and idle polling responsive") {
    CHECK(ogplay::frontend::GuiEventWaitMilliseconds(true) == 0);
    CHECK(ogplay::frontend::GuiEventWaitMilliseconds(false) == 100);
}
