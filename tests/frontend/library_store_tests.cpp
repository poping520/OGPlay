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
#include "ogplay/app/demo_games.h"
#include "apk_fixture.h"

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{};
        path = std::filesystem::temp_directory_path() /
               ("ogplay-gui-model-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
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

std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

ogplay::app::LibraryMetadata Metadata() {
    return {
        .package = "org.example.game",
        .display_name = "示例游戏",
        .version_code = 42,
        .version_name = "1.2.3",
        .imported_at = "2026-08-13T12:00:00Z",
        .profile_id = "example_profile",
    };
}

}  // namespace

TEST_CASE("GUI config is strict and round-trips UTF-8 paths") {
    TemporaryDirectory tree;
    CHECK(ogplay::app::LoadLibraryConfig(tree.path) == ogplay::app::LibraryConfig{});

    const ogplay::app::LibraryConfig config{
        .profiles_dir = std::filesystem::absolute(tree.path / std::filesystem::path(std::u8string(u8"配置"))),
    };
    ogplay::app::SaveLibraryConfig(tree.path, config);
    CHECK(ogplay::app::LoadLibraryConfig(tree.path) == config);
    ogplay::app::SaveLibraryConfig(tree.path, config);
    CHECK(ogplay::app::LoadLibraryConfig(tree.path) == config);

    Write(tree.path / "config.toml", "schema = 1\n");
    try {
        static_cast<void>(ogplay::app::LoadLibraryConfig(tree.path));
        FAIL("damaged config should fail");
    } catch (const ogplay::app::LibraryError& error) {
        CHECK(error.Code() == ogplay::app::LibraryErrorCode::corrupt_config);
    }
}

TEST_CASE("Sizes saved before the screen presets keep showing as set") {
    using namespace ogplay::app;
    TemporaryDirectory tree;
    // Every settings save wrote the old 1280×720 default: it gives way to the presets.
    Write(tree.path / "config.toml", "schema = 2\nscreen_width = 1280\nscreen_height = 720\n");
    CHECK(LoadLibraryConfig(tree.path).values.empty());
    Write(tree.path / "config.toml", "schema = 2\nscreen_width = 960\nscreen_height = 720\n");
    CHECK(Setting(LoadLibraryConfig(tree.path), "screen_resolution") == SettingValue(std::string("custom")));
    Write(tree.path / "config.toml", "schema = 2\nscreen_resolution = \"1080p\"\nscreen_width = 960\n");
    CHECK(Setting(LoadLibraryConfig(tree.path), "screen_resolution") == SettingValue(std::string("1080p")));
    // A game's own size, even the old default, is the size the user chose for it.
    const auto game = tree.path / "game";
    std::filesystem::create_directories(game);
    Write(game / "settings.toml", "schema = 1\nscreen_width = 1280\n");
    const auto settings = LoadGameSettings(game);
    CHECK(settings.values.at("screen_resolution") == SettingValue(std::string("custom")));
    CHECK(settings.values.at("screen_width") == SettingValue(std::uint32_t{1280}));
}

TEST_CASE("GUI settings validate every configured directory before save") {
    TemporaryDirectory tree;
    const auto profiles = tree.path / "profiles";
    std::filesystem::create_directories(profiles);
    CHECK_NOTHROW(ogplay::app::ValidateLibraryConfigDirectories({}));
    CHECK_NOTHROW(ogplay::app::ValidateLibraryConfigDirectories({.profiles_dir = profiles}));
    try {
        ogplay::app::ValidateLibraryConfigDirectories({.profiles_dir = tree.path / "missing"});
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
    std::filesystem::rename(tree.path / "config.toml", tree.path / "config.toml.bak");
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
    Write(tree.path / "config.toml", "schema = 2\nunknown = true\nmute = true\n");
    LibraryConfig known;
    SetSetting(known, "mute", true);
    CHECK(LoadLibraryConfig(tree.path) == known);
    for (const auto text :
         {"schema = 2\ntheme = true\n", "schema = 2\nsupersample = 0\n", "schema = 1\n", "schema = 3\n"}) {
        Write(tree.path / "config.toml", text);
        CHECK_THROWS(static_cast<void>(LoadLibraryConfig(tree.path)));
    }
}

TEST_CASE("GUI instance settings persist only overrides with strict schema and backup recovery") {
    using namespace ogplay::app;
    TemporaryDirectory tree;
    const auto first = tree.path / "first", second = tree.path / "second";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);
    CHECK(LoadGameSettings(first).values.empty());
    GameSettings settings{{{"supersample", std::uint32_t{3}}, {"mute", false}, {"model", std::string("测试机型")}}};
    SaveGameSettings(first, settings);
    CHECK(LoadGameSettings(first) == settings);
    CHECK(LoadGameSettings(second).values.empty());
    auto bad = settings;
    bad.values["supersample"] = std::uint32_t{5};
    CHECK_THROWS(SaveGameSettings(first, bad));
    CHECK(LoadGameSettings(first) == settings);
    std::filesystem::rename(first / "settings.toml", first / "settings.toml.bak");
    CHECK(LoadGameSettings(first) == settings);
    CHECK(std::filesystem::exists(first / "settings.toml.bak"));
    SaveGameSettings(first, settings);
    CHECK_FALSE(std::filesystem::exists(first / "settings.toml.bak"));
    SaveGameSettings(first, {});
    CHECK(std::filesystem::file_size(first / "settings.toml") == std::string_view("schema = 1\n").size());
    Write(first / "settings.toml", "schema = 1\nunknown = true\nmute = false\n");
    CHECK(LoadGameSettings(first) == GameSettings{{{"mute", false}}});
    for (const auto text : {"schema = 2\n", "schema = 1\nmute = 1\n", "schema = 1\nsupersample = 0\n",
                            "schema = 1\nexternal_dir = \"relative\"\n", "schema = 1\nmute = true\nmute = false\n"}) {
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
    store.Import({.source_apk = source,
                  .metadata = Metadata(),
                  .icon_png = icon,
                  .external_dir = std::filesystem::absolute(external),
                  .external_mode = ogplay::app::ExternalDataMode::link},
                 true);

    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 1);
    CHECK_FALSE(entries[0].Damaged());
    REQUIRE(entries[0].metadata.has_value());
    CHECK(*entries[0].metadata == Metadata());
    CHECK_FALSE(entries[0].external_dir.has_value());  // only settings.toml names a linked pack
    const auto linked =
        ogplay::app::EffectiveGameEntry(entries[0], ogplay::app::LoadGameSettings(entries[0].directory));
    CHECK(linked.external_dir == std::filesystem::absolute(external));
    CHECK_FALSE(linked.external_owned);
    CHECK(entries[0].icon_png == icon);
    CHECK(std::filesystem::file_size(entries[0].directory / "base.apk") == 9);
    CHECK(std::filesystem::file_size(entries[0].directory / "icon.png") == 2);
    CHECK(std::filesystem::is_regular_file(external / "keep.dat"));
    CHECK_FALSE(std::filesystem::exists(store.EntriesRoot() / ".org.example.game.importing"));
}

TEST_CASE("library records the mount only in settings.toml and only when it overrides /sdcard") {
    TemporaryDirectory tree;
    const auto source = tree.path / "source.apk";
    Write(source, "apk");
    ogplay::app::LibraryStore store(tree.path / "root");
    store.Import({.source_apk = source, .metadata = Metadata()}, true);
    store.Import({.source_apk = source, .metadata = Metadata(), .external_guest_dir = "/sdcard/Games/Example"}, true);
    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 2);
    for (const auto& entry : entries) {
        REQUIRE(entry.metadata.has_value());
        CHECK(Read(entry.directory / "meta.toml").find("external") == std::string::npos);
        CHECK(ogplay::app::LoadGameSettings(entry.directory).values.contains("external_guest_dir") ==
              (entry.key == "org.example.game-2"));
    }
    CHECK(ogplay::app::EffectiveGameEntry(entries[1], ogplay::app::LoadGameSettings(entries[1].directory))
              .external_guest_dir == "/sdcard/Games/Example");
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
    CHECK(std::filesystem::file_size(store.EntriesRoot() / "org.example.game" / "base.apk") == 5);
    CHECK(std::filesystem::file_size(store.EntriesRoot() / "org.example.game-2" / "base.apk") == 6);
    CHECK(std::filesystem::file_size(store.EntriesRoot() / "org.example.game-3" / "base.apk") == 5);
}

TEST_CASE("library replacement updates only the selected copy and preserves its identity") {
    using namespace ogplay::app;
    TemporaryDirectory tree;
    LibraryStore store(tree.path / "root");
    const auto source = tree.path / "source.apk";
    Write(source, "old apk");
    const std::vector<std::byte> icon{std::byte{1}, std::byte{2}};
    const LibraryImport initial{.source_apk = source, .metadata = Metadata(), .icon_png = icon};
    const auto first = store.Import(initial, true);
    const auto second = store.Import(initial, true);
    const auto entry = store.EntriesRoot() / second;
    const auto sandbox = store.Root() / "sandbox" / second;
    const auto identity = Read(sandbox / "meta.toml");
    Write(sandbox / "internal/save.dat", "saved progress");
    Write(entry / "settings.toml", "schema = 1\nexternal_guest_dir = \"/sdcard/Old\"\nfuture_option = true\n");
    Write(entry / "data/old.obb", "old pack");
    Write(entry / "last-run.log", "old log");
    const auto settings = Read(entry / "settings.toml");
    auto updated = Metadata();
    updated.version_code = 0;  // A downgrade is allowed, just like an upgrade.
    updated.version_name = "older";
    updated.display_name = "Updated";
    Write(source, "replacement apk");
    store.Replace(second, {.source_apk = source, .metadata = updated});
    CHECK(Read(entry / "base.apk") == "replacement apk");
    CHECK(Read(store.EntriesRoot() / first / "base.apk") == "old apk");
    CHECK(Read(entry / "settings.toml") == settings);
    CHECK(Read(entry / "data/old.obb") == "old pack");
    CHECK(Read(entry / "last-run.log") == "old log");
    CHECK(Read(sandbox / "meta.toml") == identity);
    CHECK(Read(sandbox / "internal/save.dat") == "saved progress");
    CHECK_FALSE(std::filesystem::exists(entry / "icon.png"));
    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 2);
    const auto selected =
        std::find_if(entries.begin(), entries.end(), [&](const auto& item) { return item.key == second; });
    REQUIRE(selected != entries.end());
    CHECK(selected->metadata == updated);
    // An icon from the next APK replaces the placeholder as well.
    store.Replace(second, initial);
    CHECK(Read(entry / "icon.png") == std::string("\1\2", 2));
    CHECK(store.Import(initial, true) == "org.example.game-3");
}

TEST_CASE("library replacement replaces a chosen data pack in copy link and move modes") {
    using namespace ogplay::app;
    TemporaryDirectory tree;
    LibraryStore store(tree.path / "root");
    const auto source = tree.path / "source.apk";
    const auto pack = tree.path / "pack";
    Write(source, "old apk");
    Write(pack / "new.obb", "new pack");
    auto mode = ExternalDataMode::copy;
    SUBCASE("copy") {
        mode = ExternalDataMode::copy;
    }
    SUBCASE("link") {
        mode = ExternalDataMode::link;
    }
    SUBCASE("move") {
        mode = ExternalDataMode::move;
    }
    const auto id = store.Import({.source_apk = source, .metadata = Metadata()}, true);
    const auto entry = store.EntriesRoot() / id;
    Write(entry / "data/old.obb", "old pack");
    SaveGameSettings(entry, {{{"external_guest_dir", std::string("/sdcard/Old")}, {"max_fps", std::string("60")}}});
    const auto identity = Read(store.Root() / "sandbox" / id / "meta.toml");
    store.Replace(id, {.source_apk = source, .metadata = Metadata(), .external_dir = pack, .external_mode = mode});
    const auto settings = LoadGameSettings(entry);
    CHECK(settings.values.at("max_fps") == SettingValue(std::string("60")));
    CHECK_FALSE(settings.values.contains("external_guest_dir"));
    CHECK_FALSE(std::filesystem::exists(entry / "data/old.obb"));
    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 1);
    const auto effective = EffectiveGameEntry(entries[0], settings);
    REQUIRE(effective.external_dir);
    CHECK(Read(*effective.external_dir / "new.obb") == "new pack");
    CHECK(effective.external_owned == (mode != ExternalDataMode::link));
    CHECK(effective.external_guest_dir == "/sdcard");
    CHECK(std::filesystem::exists(pack / "new.obb") == (mode != ExternalDataMode::move));
    CHECK(Read(store.Root() / "sandbox" / id / "meta.toml") == identity);
    // An APK-only update also preserves a linked pack and its settings.
    const auto saved_settings = Read(entry / "settings.toml");
    store.Replace(id, {.source_apk = source, .metadata = Metadata()});
    CHECK(Read(entry / "settings.toml") == saved_settings);
    CHECK(Read(*effective.external_dir / "new.obb") == "new pack");
    CHECK_FALSE(std::filesystem::exists(entry / ".replace-backup"));
}

TEST_CASE("library replacement rejects invalid running and cancelled requests without changing the entry") {
    using namespace ogplay::app;
    TemporaryDirectory tree;
    LibraryStore store(tree.path / "root");
    const auto source = tree.path / "source.apk";
    Write(source, "old apk");
    const auto id = store.Import({.source_apk = source, .metadata = Metadata()}, true);
    const auto entry = store.EntriesRoot() / id;
    const auto metadata = Read(entry / "meta.toml");
    Write(entry / "data/keep.dat", "keep");
    Write(source, "new apk");
    const LibraryImport request{.source_apk = source, .metadata = Metadata()};
    SUBCASE("running") {
        const RunningLease lease(store.Root(), id);
        CHECK_THROWS_AS(store.Replace(id, request), LibraryError);
    }
    SUBCASE("another package") {
        auto other = request;
        other.metadata.package = "org.example.other";
        CHECK_THROWS_AS(store.Replace(id, other), LibraryError);
    }
    SUBCASE("missing APK") {
        std::filesystem::remove(source);
        CHECK_THROWS_AS(store.Replace(id, request), LibraryError);
    }
    SUBCASE("cancel before publication") {
        ImportProgress progress;
        progress.cancel_requested.store(true);
        CHECK_THROWS_AS(store.Replace(id, request, &progress), LibraryError);
    }
    SUBCASE("link into the data being replaced") {
        auto other = request;
        other.external_dir = entry / "data";
        other.external_mode = ExternalDataMode::link;
        CHECK_THROWS_AS(store.Replace(id, other), LibraryError);
    }
    CHECK(Read(entry / "base.apk") == "old apk");
    CHECK(Read(entry / "meta.toml") == metadata);
    CHECK(Read(entry / "data/keep.dat") == "keep");
    CHECK(store.LoadEntries().size() == 1);
    CHECK_FALSE(std::filesystem::exists(entry / ".replace-backup"));
    for (const auto& item : std::filesystem::directory_iterator(store.EntriesRoot()))
        CHECK_FALSE(item.path().filename().string().ends_with(".importing"));
}

#ifndef _WIN32
TEST_CASE("library replacement rolls back files settings and data if publication fails") {
    using namespace ogplay::app;
    TemporaryDirectory tree;
    LibraryStore store(tree.path / "root");
    const auto source = tree.path / "source.apk";
    const auto pack = tree.path / "pack";
    Write(source, "old apk");
    Write(pack / "new.obb", "new pack");
    const auto id = store.Import({.source_apk = source, .metadata = Metadata()}, true);
    const auto entry = store.EntriesRoot() / id;
    Write(entry / "data/old.obb", "old pack");
    SaveGameSettings(entry, {{{"external_guest_dir", std::string("/sdcard/Old")}}});
    const auto metadata = Read(entry / "meta.toml");
    const auto settings = Read(entry / "settings.toml");
    // The icon is processed last. ELOOP forces a filesystem error after the
    // APK, metadata, settings and data pack have already been swapped.
    std::filesystem::create_symlink("icon.png", entry / "icon.png");
    Write(source, "new apk");
    CHECK_THROWS_AS(store.Replace(id, {.source_apk = source, .metadata = Metadata(), .external_dir = pack}),
                    LibraryError);
    CHECK(Read(entry / "base.apk") == "old apk");
    CHECK(Read(entry / "meta.toml") == metadata);
    CHECK(Read(entry / "settings.toml") == settings);
    CHECK(Read(entry / "data/old.obb") == "old pack");
    CHECK_FALSE(std::filesystem::exists(entry / "data/new.obb"));
    CHECK(Read(pack / "new.obb") == "new pack");
    CHECK(std::filesystem::is_symlink(entry / "icon.png"));
    CHECK_FALSE(std::filesystem::exists(entry / ".replace-backup"));
    for (const auto& item : std::filesystem::directory_iterator(store.EntriesRoot()))
        CHECK_FALSE(item.path().filename().string().ends_with(".importing"));
}
#endif

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
    for (std::size_t i = 0; i < errors.size(); ++i)
        if (errors[i]) {
            try {
                std::rethrow_exception(errors[i]);
            } catch (const ogplay::app::LibraryError& e) {
                CHECK(e.Code() == ogplay::app::LibraryErrorCode::busy);
            }
            store.Import({.source_apk = sources[i], .metadata = Metadata()}, true);
        }
    CHECK(std::filesystem::is_directory(store.EntriesRoot() / "org.example.game"));
    CHECK(std::filesystem::is_directory(store.EntriesRoot() / "org.example.game-2"));
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
    std::filesystem::create_directories(store.EntriesRoot() / "org.example.game");
    std::filesystem::create_directories(tree.path / "root" / "sandbox" / "org.example.game-3");
    std::filesystem::create_directories(store.EntriesRoot() / "org.example.game.similar");
    CHECK(store.NextInstallationId("org.example.game") == "org.example.game-2");
    std::filesystem::create_directories(tree.path / "root" / "sandbox" / "org.example.game-2");
    CHECK(store.NextInstallationId("org.example.game") == "org.example.game-4");
    std::filesystem::remove_all(store.EntriesRoot() / "org.example.game");
    CHECK(store.NextInstallationId("org.example.game") == "org.example.game");
}

TEST_CASE("library exposes corrupt entries instead of silently skipping them") {
    TemporaryDirectory tree;
    ogplay::app::LibraryStore store(tree.path / "root");
    Write(store.EntriesRoot() / "org.example.bad" / "meta.toml",
          "schema = 1\npackage = \"org.example.other\"\n"
          "display_name = \"Bad\"\nversion_code = 1\nversion_name = \"1\"\n"
          "imported_at = \"now\"\n");
    Write(store.EntriesRoot() / "org.example.bad" / "base.apk", "apk");
    Write(store.EntriesRoot() / "org.example.missing" / "meta.toml",
          "schema = 1\npackage = \"org.example.missing\"\n"
          "display_name = \"Missing\"\nversion_code = 1\nversion_name = \"1\"\n"
          "imported_at = \"now\"\nunknown = true\n");

    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].Damaged());
    CHECK(entries[1].Damaged());
    CHECK(entries[0].damage_reason->find("match") != std::string::npos);
    CHECK(entries[1].damage_reason->find("missing") != std::string::npos);
}

TEST_CASE("library migrates a legacy game.apk copy to base.apk") {
    TemporaryDirectory tree;
    ogplay::app::LibraryStore store(tree.path / "root");
    const auto directory = store.EntriesRoot() / "org.example.legacy";
    Write(directory / "meta.toml", "schema = 1\npackage = \"org.example.legacy\"\n"
                                   "display_name = \"Legacy\"\nversion_code = 1\nversion_name = \"1\"\n"
                                   "imported_at = \"now\"\n");
    Write(directory / "game.apk", "legacy");
    CHECK(ogplay::app::LibraryApkPath(directory) == directory / "game.apk");

    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 1);
    CHECK_FALSE(entries[0].Damaged());
    CHECK_FALSE(std::filesystem::exists(directory / "game.apk"));
    CHECK(std::filesystem::file_size(directory / "base.apk") == 6);
    CHECK(ogplay::app::LibraryApkPath(directory) == directory / "base.apk");
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
    store.Import({.source_apk = source,
                  .metadata = Metadata(),
                  .external_dir = std::filesystem::absolute(external),
                  .external_mode = ogplay::app::ExternalDataMode::link},
                 true);

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
    std::filesystem::create_directories(store.EntriesRoot() / ".org.example.game.importing");

    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 1);
    CHECK_FALSE(std::filesystem::exists(store.EntriesRoot() / ".org.example.game.importing"));
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
    CHECK_THROWS_AS(store.Import({.source_apk = tree.path / "missing.apk", .metadata = metadata}, true),
                    ogplay::app::LibraryError);
    CHECK_FALSE(std::filesystem::exists(store.EntriesRoot() / ".org.example.game.importing"));
}

TEST_CASE("library copies owned external data and removes it with the entry") {
    TemporaryDirectory tree;
    const auto source = tree.path / "source.apk";
    const auto external = tree.path / "download" / "Pack";
    const auto sandbox = tree.path / "root" / "sandbox" / "org.example.game";
    Write(source, "apk");
    Write(external / "data" / "Level1.bin", "level-one");
    Write(external / "Readme.txt", "hi");
    std::filesystem::create_directories(external / "empty");
    ogplay::app::LibraryStore store(tree.path / "root");
    ogplay::app::ImportProgress progress;
    const auto id = store.Import(
        {.source_apk = source, .metadata = Metadata(), .external_dir = std::filesystem::absolute(external)}, true,
        &progress);

    CHECK(progress.total_bytes == 11);
    CHECK(progress.copied_bytes == 11);
    const auto entry = store.EntriesRoot() / id;
    CHECK(Read(entry / "meta.toml").find("external") == std::string::npos);
    CHECK_FALSE(std::filesystem::exists(entry / "settings.toml"));
    CHECK(Read(entry / "data" / "data" / "Level1.bin") == "level-one");
    CHECK(Read(entry / "data" / "Readme.txt") == "hi");
    CHECK(std::filesystem::is_directory(entry / "data" / "empty"));
    CHECK_FALSE(std::filesystem::exists(tree.path / "root" / "import-staging" / "data-0"));

    // The library copy no longer depends on the selected download directory.
    std::filesystem::remove_all(tree.path / "download");
    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].metadata.has_value());
    CHECK(entries[0].external_owned);
    CHECK(entries[0].external_dir == (entry / "data").lexically_normal());
    const auto launch = ogplay::app::ResolveLaunch(store, id);
    CHECK(launch.options.external_directory == (entry / "data").lexically_normal());
    CHECK(launch.options.external_guest_directory == "/sdcard");

    // A different per-game directory links; the owned copy is left unused.
    const auto other = tree.path / "other";
    std::filesystem::create_directories(other);
    const auto linked = ogplay::app::EffectiveGameEntry(entries[0], {{{"external_dir", other.generic_string()}}});
    CHECK_FALSE(linked.external_owned);
    CHECK(linked.external_dir == other);
    const auto same =
        ogplay::app::EffectiveGameEntry(entries[0], {{{"external_dir", entries[0].external_dir->generic_string()}}});
    CHECK(same.external_owned);

    Write(sandbox / "save.bin", "save");
    store.Remove(id);
    CHECK_FALSE(std::filesystem::exists(entry));
    CHECK(std::filesystem::is_regular_file(sandbox / "save.bin"));
}

TEST_CASE("owned external data import rejects links, overlap and cancellation") {
    TemporaryDirectory tree;
    const auto source = tree.path / "source.apk";
    Write(source, "apk");
    ogplay::app::LibraryStore store(tree.path / "root");
    const auto owned = [&](const std::filesystem::path& external) {
        return ogplay::app::LibraryImport{
            .source_apk = source, .metadata = Metadata(), .external_dir = std::filesystem::absolute(external)};
    };
    const auto staged_files = [&] {
        std::size_t count = 0;
        const auto staging = tree.path / "root" / "import-staging";
        if (!std::filesystem::exists(staging)) return count;
        for (const auto& item : std::filesystem::recursive_directory_iterator(staging))
            if (item.is_regular_file() && item.path().extension() != ".lock") ++count;
        return count;
    };

    const auto linked = tree.path / "linked";
    Write(linked / "real.bin", "data");
    std::error_code symlink_error;
    // Windows hosts without symlink privilege skip only this case.
    std::filesystem::create_symlink(linked / "real.bin", linked / "alias.bin", symlink_error);
    if (!symlink_error) {
        CHECK_THROWS_AS(store.Import(owned(linked), true), ogplay::app::LibraryError);
    }

    CHECK_THROWS_AS(store.Import(owned(tree.path), true), ogplay::app::LibraryError);

    const auto plain = tree.path / "plain";
    Write(plain / "a.bin", "aaaa");
    ogplay::app::ImportProgress cancelled;
    cancelled.cancel_requested = true;
    try {
        static_cast<void>(store.Import(owned(plain), true, &cancelled));
        FAIL("cancelled import must not publish");
    } catch (const ogplay::app::LibraryError& error) {
        CHECK(std::string(error.what()).find("import_cancelled") != std::string::npos);
    }
    CHECK(store.LoadEntries().empty());
    CHECK(staged_files() == 0);

    // An interrupted import leaves an unlocked staging directory behind.
    Write(tree.path / "root" / "import-staging" / "data-7" / "partial.bin", "partial");
    CHECK(staged_files() == 1);
    static_cast<void>(store.Import(owned(plain), true));
    CHECK(staged_files() == 0);
    CHECK(Read(store.EntriesRoot() / "org.example.game" / "data" / "a.bin") == "aaaa");
}

TEST_CASE("re-import adopts saves left by a removed entry and keeps its id") {
    TemporaryDirectory tree;
    const auto source = tree.path / "source.apk";
    Write(source, "apk");
    ogplay::app::LibraryStore store(tree.path / "root");
    const auto saves = tree.path / "root" / "sandbox";
    CHECK(store.Import({.source_apk = source, .metadata = Metadata()}, true) == "org.example.game");
    CHECK(store.Import({.source_apk = source, .metadata = Metadata()}, true) == "org.example.game-2");
    Write(saves / "org.example.game-2" / "internal" / "save.bin", "progress");
    const auto meta = Read(saves / "org.example.game-2" / "meta.toml");

    store.Remove("org.example.game-2");
    CHECK_FALSE(store.ReusableInstallationId("org.example.other").has_value());
    CHECK(store.ReusableInstallationId("org.example.game") == "org.example.game-2");
    CHECK(store.Import({.source_apk = source, .metadata = Metadata()}, true) == "org.example.game-2");
    CHECK(Read(saves / "org.example.game-2" / "internal" / "save.bin") == "progress");
    CHECK(Read(saves / "org.example.game-2" / "meta.toml") == meta);

    // Deleting saves frees the id; the next import starts from a fresh sandbox.
    store.Remove("org.example.game-2", true);
    CHECK_FALSE(std::filesystem::exists(saves / "org.example.game-2"));
    CHECK_FALSE(store.ReusableInstallationId("org.example.game").has_value());
    CHECK(store.Import({.source_apk = source, .metadata = Metadata()}, true) == "org.example.game-2");
    CHECK_FALSE(std::filesystem::exists(saves / "org.example.game-2" / "internal" / "save.bin"));

    // Saves whose meta names another package are never adopted.
    Write(saves / "org.example.game-5" / "meta.toml", "schema = 3\npackage = \"org.example.other\"\n");
    store.Remove("org.example.game-2", true);
    CHECK_FALSE(store.ReusableInstallationId("org.example.game").has_value());
}

TEST_CASE("bundled demo games seed once while shown and come back when shown again") {
    using namespace ogplay::app;
    TemporaryDirectory root;
    const auto demos_dir = root.path / "demos";
    CHECK(FindBundledDemos(demos_dir, ProfileCatalog{}).empty());
    const auto apk = ogplay::test::MinimalApk();
    Write(demos_dir / "demo.apk", std::string_view(reinterpret_cast<const char*>(apk.data()), apk.size()));
    Write(demos_dir / "readme.txt", "not a demo");
    const auto demos = FindBundledDemos(demos_dir, ProfileCatalog{});
    REQUIRE(demos.size() == 1);
    CHECK(IsBundledDemo(demos, "org.example.game"));
    CHECK_FALSE(IsBundledDemo(demos, "org.example.other"));

    LibraryStore store(root.path / "library");
    // A new library shows demos by default, so the first start adds the demo.
    const auto first = SeedBundledDemos(store, demos, "2026-11-03T00:00:00Z");
    REQUIRE(first.size() == 1);
    CHECK(store.LoadEntries().size() == 1);
    CHECK(SeedBundledDemos(store, demos, "2026-11-03T00:00:01Z").empty());

    store.Remove(first.front());
    auto config = LoadLibraryConfig(store.Root());
    SetSetting(config, "show_demo_games", false);
    SaveLibraryConfig(store.Root(), config);
    CHECK(SeedBundledDemos(store, demos, "2026-11-03T00:00:02Z").empty());
    CHECK(store.LoadEntries().empty());
    SetSetting(config, "show_demo_games", true);
    SaveLibraryConfig(store.Root(), config);
    CHECK(SeedBundledDemos(store, demos, "2026-11-03T00:00:03Z") == first);
}
