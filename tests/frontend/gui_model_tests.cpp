#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ogplay/frontend/gui_model.h"

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{};
        path = std::filesystem::temp_directory_path() /
               ("ogplay-gui-model-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
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
    REQUIRE(output);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    REQUIRE(output);
}

ogplay::frontend::LibraryMetadata Metadata(const std::filesystem::path& external = {}) {
    ogplay::frontend::LibraryMetadata metadata{
        .package = "org.example.game",
        .display_name = "示例游戏",
        .version_code = 42,
        .version_name = "1.2.3",
        .imported_at = "2026-08-13T12:00:00Z",
        .profile_id = "example_profile",
    };
    if (!external.empty()) metadata.external_dir = std::filesystem::absolute(external);
    return metadata;
}

}  // namespace

TEST_CASE("GUI config is strict and round-trips UTF-8 paths") {
    TemporaryDirectory tree;
    CHECK(ogplay::frontend::LoadGuiConfig(tree.path) == ogplay::frontend::GuiConfig{});

    const ogplay::frontend::GuiConfig config{
        .system_dir = std::filesystem::absolute(
            tree.path / std::filesystem::path(std::u8string(u8"系统库"))),
        .profiles_dir = std::filesystem::absolute(tree.path / "profiles"),
    };
    ogplay::frontend::SaveGuiConfig(tree.path, config);
    CHECK(ogplay::frontend::LoadGuiConfig(tree.path) == config);
    ogplay::frontend::SaveGuiConfig(tree.path, config);
    CHECK(ogplay::frontend::LoadGuiConfig(tree.path) == config);

    Write(tree.path / "config.toml", "schema = 1\nunknown = \"x\"\n");
    try {
        static_cast<void>(ogplay::frontend::LoadGuiConfig(tree.path));
        FAIL("damaged config should fail");
    } catch (const ogplay::frontend::GuiModelError& error) {
        CHECK(error.Code() == ogplay::frontend::GuiModelErrorCode::corrupt_config);
    }
}

TEST_CASE("GUI settings validate every configured directory before save") {
    TemporaryDirectory tree;
    const auto system = tree.path / "system";
    const auto profiles = tree.path / "profiles";
    std::filesystem::create_directories(system);
    std::filesystem::create_directories(profiles);
    CHECK_NOTHROW(ogplay::frontend::ValidateGuiConfigDirectories({}));
    CHECK_NOTHROW(ogplay::frontend::ValidateGuiConfigDirectories(
        {system, profiles}));
    CHECK_THROWS_AS(ogplay::frontend::ValidateGuiConfigDirectories(
                        {tree.path / "missing-system", std::nullopt}),
                    ogplay::frontend::GuiModelError);
    try {
        ogplay::frontend::ValidateGuiConfigDirectories(
            {system, tree.path / "missing"});
        FAIL("missing Profile directory should fail");
    } catch (const ogplay::frontend::GuiModelError& error) {
        CHECK(error.Code() == ogplay::frontend::GuiModelErrorCode::not_found);
        CHECK(error.Path() == tree.path / "missing");
    }
}

TEST_CASE("GUI config recovers an interrupted replacement backup") {
    TemporaryDirectory tree;
    const auto system = std::filesystem::absolute(tree.path / "system");
    std::filesystem::create_directories(system);
    ogplay::frontend::SaveGuiConfig(tree.path, {.system_dir = system});
    std::filesystem::rename(tree.path / "config.toml",
                            tree.path / "config.toml.bak");
    const auto recovered = ogplay::frontend::LoadGuiConfig(tree.path);
    CHECK(recovered.system_dir == system);
    CHECK(std::filesystem::is_regular_file(tree.path / "config.toml"));
    CHECK_FALSE(std::filesystem::exists(tree.path / "config.toml.bak"));
}

TEST_CASE("library import atomically copies APK metadata and optional icon") {
    TemporaryDirectory tree;
    const auto source = tree.path / "source.apk";
    Write(source, "apk-bytes");
    const auto external = tree.path / "external-data";
    std::filesystem::create_directories(external);
    Write(external / "keep.dat", "external");

    ogplay::frontend::LibraryStore store(tree.path / "root");
    const std::vector<std::byte> icon{std::byte{0x89}, std::byte{0x50}};
    store.Import({.source_apk = source, .metadata = Metadata(external), .icon_png = icon});

    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 1);
    CHECK_FALSE(entries[0].Damaged());
    REQUIRE(entries[0].metadata.has_value());
    CHECK(*entries[0].metadata == Metadata(external));
    CHECK(entries[0].icon_png == icon);
    CHECK(std::filesystem::file_size(entries[0].directory / "game.apk") == 9);
    CHECK(std::filesystem::file_size(entries[0].directory / "icon.png") == 2);
    CHECK(std::filesystem::is_regular_file(external / "keep.dat"));
    CHECK_FALSE(std::filesystem::exists(
        store.EntriesRoot() / ".org.example.game.importing"));
}

TEST_CASE("library rejects duplicate package without replacing original") {
    TemporaryDirectory tree;
    const auto first = tree.path / "first.apk";
    const auto second = tree.path / "second.apk";
    Write(first, "first");
    Write(second, "second");
    ogplay::frontend::LibraryStore store(tree.path / "root");
    store.Import({.source_apk = first, .metadata = Metadata()});

    try {
        store.Import({.source_apk = second, .metadata = Metadata()});
        FAIL("duplicate package should fail");
    } catch (const ogplay::frontend::GuiModelError& error) {
        CHECK(error.Code() == ogplay::frontend::GuiModelErrorCode::duplicate_package);
    }
    CHECK(std::filesystem::file_size(
              store.EntriesRoot() / "org.example.game" / "game.apk") == 5);
}

TEST_CASE("library exposes corrupt entries instead of silently skipping them") {
    TemporaryDirectory tree;
    ogplay::frontend::LibraryStore store(tree.path / "root");
    Write(store.EntriesRoot() / "org.example.bad" / "meta.toml",
          "schema = 1\npackage = \"org.example.other\"\n"
          "display_name = \"Bad\"\nversion_code = 1\nversion_name = \"1\"\n"
          "imported_at = \"now\"\n");
    Write(store.EntriesRoot() / "org.example.bad" / "game.apk", "apk");
    Write(store.EntriesRoot() / "org.example.missing" / "meta.toml",
          "schema = 1\npackage = \"org.example.missing\"\n"
          "display_name = \"Missing\"\nversion_code = 1\nversion_name = \"1\"\n"
          "imported_at = \"now\"\n");

    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].Damaged());
    CHECK(entries[1].Damaged());
    CHECK(entries[0].damage_reason->find("match") != std::string::npos);
    CHECK(entries[1].damage_reason->find("missing") != std::string::npos);
}

TEST_CASE("library removal never touches external data or persistent sandbox") {
    TemporaryDirectory tree;
    const auto source = tree.path / "source.apk";
    const auto external = tree.path / "external";
    const auto sandbox = tree.path / "root" / "sandbox" / "org.example.game";
    Write(source, "apk");
    Write(external / "data.bin", "external");
    Write(sandbox / "save.bin", "save");
    ogplay::frontend::LibraryStore store(tree.path / "root");
    store.Import({.source_apk = source, .metadata = Metadata(external)});

    store.Remove("org.example.game");
    CHECK_FALSE(std::filesystem::exists(store.EntriesRoot() / "org.example.game"));
    CHECK(std::filesystem::is_regular_file(external / "data.bin"));
    CHECK(std::filesystem::is_regular_file(sandbox / "save.bin"));
    CHECK_THROWS_AS(store.Remove("../sandbox"), ogplay::frontend::GuiModelError);
}

TEST_CASE("library cleans all stale import temporary directories") {
    TemporaryDirectory tree;
    ogplay::frontend::LibraryStore store(tree.path / "root");
    std::filesystem::create_directories(store.EntriesRoot() / ".damaged");
    std::filesystem::create_directories(
        store.EntriesRoot() / ".org.example.game.importing");

    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 1);
    CHECK_FALSE(std::filesystem::exists(
        store.EntriesRoot() / ".org.example.game.importing"));
    CHECK(entries[0].key == ".damaged");
    CHECK(entries[0].Damaged());
    store.Remove(".damaged");
    CHECK_FALSE(std::filesystem::exists(store.EntriesRoot() / ".damaged"));
}

TEST_CASE("invalid import leaves no temporary library entry") {
    TemporaryDirectory tree;
    ogplay::frontend::LibraryStore store(tree.path / "root");
    auto metadata = Metadata();
    metadata.display_name.clear();
    CHECK_THROWS_AS(store.Import({.source_apk = tree.path / "missing.apk",
                                  .metadata = metadata}),
                    ogplay::frontend::GuiModelError);
    CHECK_FALSE(std::filesystem::exists(
        store.EntriesRoot() / ".org.example.game.importing"));
}
