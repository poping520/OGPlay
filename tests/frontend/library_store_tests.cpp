#include <doctest/doctest.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <exception>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <string>
#include <thread>
#include <vector>

#include "ogplay/app/library.h"
#include "ogplay/app/library.h"

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

ogplay::app::LibraryMetadata Metadata(const std::filesystem::path& external = {}) {
    ogplay::app::LibraryMetadata metadata{
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
    CHECK(ogplay::app::LoadLibraryConfig(tree.path) == ogplay::app::LibraryConfig{});

    const ogplay::app::LibraryConfig config{
        .profiles_dir = std::filesystem::absolute(
            tree.path / std::filesystem::path(std::u8string(u8"配置"))),
    };
    ogplay::app::SaveLibraryConfig(tree.path, config);
    CHECK(ogplay::app::LoadLibraryConfig(tree.path) == config);
    ogplay::app::SaveLibraryConfig(tree.path, config);
    CHECK(ogplay::app::LoadLibraryConfig(tree.path) == config);

    Write(tree.path / "config.toml", "schema = 1\nunknown = \"x\"\n");
    try {
        static_cast<void>(ogplay::app::LoadLibraryConfig(tree.path));
        FAIL("damaged config should fail");
    } catch (const ogplay::app::LibraryError& error) {
        CHECK(error.Code() == ogplay::app::LibraryErrorCode::corrupt_config);
    }
}

TEST_CASE("GUI settings validate every configured directory before save") {
    TemporaryDirectory tree;
    const auto profiles = tree.path / "profiles";
    std::filesystem::create_directories(profiles);
    CHECK_NOTHROW(ogplay::app::ValidateLibraryConfigDirectories({}));
    CHECK_NOTHROW(ogplay::app::ValidateLibraryConfigDirectories(
        {.profiles_dir = profiles}));
    try {
        ogplay::app::ValidateLibraryConfigDirectories(
            {.profiles_dir = tree.path / "missing"});
        FAIL("missing Profile directory should fail");
    } catch (const ogplay::app::LibraryError& error) {
        CHECK(error.Code() == ogplay::app::LibraryErrorCode::not_found);
        CHECK(error.Path() == tree.path / "missing");
    }
}

TEST_CASE("GUI config recovers an interrupted replacement backup") {
    TemporaryDirectory tree;
    const auto profiles = std::filesystem::absolute(tree.path / "profiles");
    std::filesystem::create_directories(profiles);
    ogplay::app::SaveLibraryConfig(tree.path, {.profiles_dir = profiles});
    std::filesystem::rename(tree.path / "config.toml",
                            tree.path / "config.toml.bak");
    // Shared-lock readers use the backup read-only; the next save republishes it.
    const auto recovered = ogplay::app::LoadLibraryConfig(tree.path);
    CHECK(recovered.profiles_dir == profiles);
    CHECK_FALSE(std::filesystem::exists(tree.path / "config.toml"));
    CHECK(std::filesystem::is_regular_file(tree.path / "config.toml.bak"));
    ogplay::app::SaveLibraryConfig(tree.path, recovered);
    CHECK(std::filesystem::is_regular_file(tree.path / "config.toml"));
    CHECK_FALSE(std::filesystem::exists(tree.path / "config.toml.bak"));
    CHECK(ogplay::app::LoadLibraryConfig(tree.path).profiles_dir == profiles);
}

TEST_CASE("GUI config discards the legacy external system directory") {
    TemporaryDirectory tree;
    Write(tree.path / "config.toml",
          "schema = 1\nsystem_dir = \"C:/old-bionic\"\n");
    CHECK(ogplay::app::LoadLibraryConfig(tree.path) ==
          ogplay::app::LibraryConfig{});
    ogplay::app::SaveLibraryConfig(tree.path,
                                    ogplay::app::LibraryConfig{});
    CHECK(std::filesystem::file_size(tree.path / "config.toml") ==
          std::string_view("schema = 2\n").size());
}

TEST_CASE("GUI schema two validates typed settings without replacing good config") {
    using namespace ogplay::app;
    TemporaryDirectory tree;
    LibraryConfig config;
    SetSetting(config, "theme", std::string("light"));
    SetSetting(config, "supersample", std::uint32_t{3});
    SetSetting(config, "mute", true);
    SaveLibraryConfig(tree.path, config);
    CHECK(LoadLibraryConfig(tree.path) == config);
    CHECK(std::get<bool>(Setting(config, "show_exit_log")));
    CHECK_THROWS(SetSetting(config, "theme", std::string("unknown")));
    CHECK_THROWS(SetSetting(config, "supersample", std::uint32_t{5}));
    CHECK_THROWS(SetSetting(config, "mute", std::string("true")));
    CHECK_THROWS(SetSetting(config, "unknown", true));
    CHECK(LoadLibraryConfig(tree.path) == config);
    auto invalid = config;
    invalid.values["mcp_port"] = std::uint32_t{0};
    CHECK_THROWS(SaveLibraryConfig(tree.path, invalid));
    CHECK(LoadLibraryConfig(tree.path) == config);
    for (const auto text : {"schema = 2\ntheme = true\n", "schema = 2\nsupersample = 0\n",
                            "schema = 2\nunknown = true\n", "schema = 3\n"}) {
        Write(tree.path / "config.toml", text);
        CHECK_THROWS(static_cast<void>(LoadLibraryConfig(tree.path)));
    }
}

TEST_CASE("GUI instance settings persist only overrides with strict schema and backup recovery") {
    using namespace ogplay::app;
    TemporaryDirectory tree;
    const auto first = tree.path / "first", second = tree.path / "second";
    std::filesystem::create_directories(first); std::filesystem::create_directories(second);
    CHECK(LoadGameSettings(first).values.empty());
    GameSettings settings{{{"supersample", std::uint32_t{3}}, {"mute", false}, {"model", std::string("测试机型")}}};
    SaveGameSettings(first, settings);
    CHECK(LoadGameSettings(first) == settings);
    CHECK(LoadGameSettings(second).values.empty());
    auto bad = settings; bad.values["supersample"] = std::uint32_t{5};
    CHECK_THROWS(SaveGameSettings(first, bad));
    CHECK(LoadGameSettings(first) == settings);
    std::filesystem::rename(first / "settings.toml", first / "settings.toml.bak");
    CHECK(LoadGameSettings(first) == settings);
    CHECK(std::filesystem::exists(first / "settings.toml.bak"));
    SaveGameSettings(first, settings);
    CHECK_FALSE(std::filesystem::exists(first / "settings.toml.bak"));
    SaveGameSettings(first, {});
    CHECK(std::filesystem::file_size(first / "settings.toml") == std::string_view("schema = 1\n").size());
    for (const auto text : {"schema = 2\n", "schema = 1\nunknown = true\n", "schema = 1\nmute = 1\n",
                            "schema = 1\nsupersample = 0\n", "schema = 1\nexternal_dir = \"relative\"\n",
                            "schema = 1\nmute = true\nmute = false\n"}) {
        Write(first / "settings.toml", text);
        CHECK_THROWS(static_cast<void>(LoadGameSettings(first)));
    }
}

TEST_CASE("library import atomically copies APK metadata and optional icon") {
    TemporaryDirectory tree;
    const auto source = tree.path / "source.apk";
    Write(source, "apk-bytes");
    const auto external = tree.path / "external-data";
    std::filesystem::create_directories(external);
    Write(external / "keep.dat", "external");

    ogplay::app::LibraryStore store(tree.path / "root");
    const std::vector<std::byte> icon{std::byte{0x89}, std::byte{0x50}};
    store.Import({.source_apk = source, .metadata = Metadata(external), .icon_png = icon}, true);

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

TEST_CASE("library writes external_guest_dir only when it overrides /sdcard") {
    TemporaryDirectory tree;
    const auto source = tree.path / "source.apk";
    Write(source, "apk");
    ogplay::app::LibraryStore store(tree.path / "root");
    store.Import({.source_apk = source, .metadata = Metadata()}, true);
    auto metadata = Metadata();
    metadata.external_guest_dir = "/sdcard/Games/Example";
    store.Import({.source_apk = source, .metadata = metadata}, true);
    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 2);
    for (const auto& entry : entries) {
        REQUIRE(entry.metadata.has_value());
        std::ifstream input(entry.directory / "meta.toml", std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(input), {}};
        const bool overridden = entry.metadata->external_guest_dir != "/sdcard";
        CHECK(overridden == (text.find("external_guest_dir") != std::string::npos));
    }
    CHECK(std::ranges::count(entries, std::string("/sdcard/Games/Example"),
                             [](const auto& entry) { return entry.metadata->external_guest_dir; }) == 1);
}

TEST_CASE("library preserves Android version code zero") {
    TemporaryDirectory tree;
    const auto source = tree.path / "source.apk";
    Write(source, "apk");
    auto metadata = Metadata();
    metadata.version_code = 0;
    ogplay::app::LibraryStore store(tree.path / "root");
    store.Import({.source_apk = source, .metadata = metadata}, true);
    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].metadata.has_value());
    CHECK(entries[0].metadata->version_code == 0);
}

TEST_CASE("library assigns a distinct installation id to duplicate packages") {
    TemporaryDirectory tree;
    const auto first = tree.path / "first.apk";
    const auto second = tree.path / "second.apk";
    const auto third = tree.path / "third.apk";
    Write(first, "first");
    Write(second, "second");
    Write(third, "third");
    ogplay::app::LibraryStore store(tree.path / "root");
    store.Import({.source_apk = first, .metadata = Metadata()}, true);

    store.Import({.source_apk = second, .metadata = Metadata()}, true);
    auto missing_version = Metadata();
    missing_version.version_code = 0;
    missing_version.version_name.clear();
    store.Import({.source_apk = third, .metadata = missing_version}, true);
    CHECK(std::filesystem::file_size(
              store.EntriesRoot() / "org.example.game" / "game.apk") == 5);
    CHECK(std::filesystem::file_size(
              store.EntriesRoot() / "org.example.game-2" / "game.apk") == 6);
    CHECK(std::filesystem::file_size(
              store.EntriesRoot() / "org.example.game-3" / "game.apk") == 5);
}

TEST_CASE("concurrent library imports reject busy operations and can be retried") {
    TemporaryDirectory tree;
    const auto first = tree.path / "first.apk";
    const auto second = tree.path / "second.apk";
    Write(first, "first");
    Write(second, "second");
    ogplay::app::LibraryStore store(tree.path / "root");
    std::array<std::exception_ptr, 2> errors{};
    std::thread left([&] {
        try {
            store.Import({.source_apk = first, .metadata = Metadata()}, true);
        } catch (...) {
            errors[0] = std::current_exception();
        }
    });
    std::thread right([&] {
        try {
            store.Import({.source_apk = second, .metadata = Metadata()}, true);
        } catch (...) {
            errors[1] = std::current_exception();
        }
    });
    left.join();
    right.join();
    // Nonblocking OS locks explicitly reject the competing operation. Retrying
    // after it completes allocates the next free installation id.
    const std::array sources{first, second};
    for (std::size_t i = 0; i < errors.size(); ++i) if (errors[i]) {
        try { std::rethrow_exception(errors[i]); }
        catch (const ogplay::app::LibraryError& e) { CHECK(e.Code() == ogplay::app::LibraryErrorCode::busy); }
        store.Import({.source_apk = sources[i], .metadata = Metadata()}, true);
    }
    CHECK(std::filesystem::is_directory(
        store.EntriesRoot() / "org.example.game"));
    CHECK(std::filesystem::is_directory(
        store.EntriesRoot() / "org.example.game-2"));
    for (const auto id : {"org.example.game", "org.example.game-2"}) {
        const auto sandbox = tree.path / "root" / "sandbox" / id;
        CHECK(std::filesystem::is_regular_file(sandbox / "meta.toml"));
        CHECK(std::filesystem::is_directory(sandbox / "internal"));
        CHECK(std::filesystem::is_directory(sandbox / "external"));
        CHECK(std::filesystem::is_directory(sandbox / "obb"));
        CHECK(std::filesystem::is_directory(sandbox / "sdcard"));
    }
}

TEST_CASE("library installation ids use the union of library and sandbox holes") {
    TemporaryDirectory tree;
    ogplay::app::LibraryStore store(tree.path / "root");
    std::filesystem::create_directories(
        store.EntriesRoot() / "org.example.game");
    std::filesystem::create_directories(
        tree.path / "root" / "sandbox" / "org.example.game-3");
    std::filesystem::create_directories(
        store.EntriesRoot() / "org.example.game.similar");
    CHECK(store.NextInstallationId("org.example.game") ==
          "org.example.game-2");
    std::filesystem::create_directories(
        tree.path / "root" / "sandbox" / "org.example.game-2");
    CHECK(store.NextInstallationId("org.example.game") ==
          "org.example.game-4");
    std::filesystem::remove_all(store.EntriesRoot() / "org.example.game");
    CHECK(store.NextInstallationId("org.example.game") ==
          "org.example.game");
}

TEST_CASE("library exposes corrupt entries instead of silently skipping them") {
    TemporaryDirectory tree;
    ogplay::app::LibraryStore store(tree.path / "root");
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
    ogplay::app::LibraryStore store(tree.path / "root");
    store.Import({.source_apk = source, .metadata = Metadata(external)}, true);

    store.Remove("org.example.game-2");
    CHECK_FALSE(std::filesystem::exists(store.EntriesRoot() / "org.example.game-2"));
    CHECK(std::filesystem::is_regular_file(external / "data.bin"));
    CHECK(std::filesystem::is_regular_file(sandbox / "save.bin"));
    CHECK_THROWS_AS(store.Remove("../sandbox"), ogplay::app::LibraryError);
}

TEST_CASE("library cleans all stale import temporary directories") {
    TemporaryDirectory tree;
    ogplay::app::LibraryStore store(tree.path / "root");
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
    ogplay::app::LibraryStore store(tree.path / "root");
    auto metadata = Metadata();
    metadata.display_name.clear();
    CHECK_THROWS_AS(store.Import({.source_apk = tree.path / "missing.apk",
                                  .metadata = metadata}, true),
                    ogplay::app::LibraryError);
    CHECK_FALSE(std::filesystem::exists(
        store.EntriesRoot() / ".org.example.game.importing"));
}
