#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
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
