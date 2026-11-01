#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "frontend/gui/rpc.h"
#include "ogplay/app/data_hint.h"
#include "ogplay/app/json.h"
#include "ogplay/app/library.h"

namespace {
using namespace ogplay::app;

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{};
        path = std::filesystem::temp_directory_path() /
               ("ogplay-data-hint-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        static_cast<void>(std::filesystem::remove_all(path, error));
    }
    std::filesystem::path path;
};

void Write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// A pack laid out like a vendor data directory: data/ with nested assets.
std::filesystem::path Pack(const std::filesystem::path& root) {
    Write(root / "Pack" / "data" / "3D" / "camera" / "intro.bdae", "1");
    Write(root / "Pack" / "data" / "menus" / "map.tga", "2");
    Write(root / "Pack" / "Readme.txt", "3");
    return root / "Pack";
}

}  // namespace

TEST_CASE("ADR-0146 infers the mount from misses that exist in the data pack") {
    TemporaryDirectory tree;
    const auto pack = Pack(tree.path);
    MissingDataReport report;
    report.missing = {"/sdcard/vendor/games/title/data/3d/camera/intro.bdae",
                      "/sdcard/vendor/games/title/data/menus/map.tga",
                      "/sdcard/vendor/games/title/data/menus/absent.tga",
                      "/sdcard/unrelated/readme.txt"};
    const auto hint = InferDataMount(report, pack, "/sdcard");
    CHECK(hint.kind == DataMountHintKind::remount);
    REQUIRE(hint.candidates.size() == 1);
    CHECK(hint.candidates[0].guest_dir == "/sdcard/vendor/games/title");
    CHECK(hint.candidates[0].explained == 2);
    CHECK(hint.candidates[0].examples.size() == 2);
    CHECK(hint.missing == 4);

    // Already mounted there: nothing new to suggest.
    CHECK(InferDataMount(report, pack, "/sdcard/Vendor/Games/Title").kind == DataMountHintKind::none);
    // One shallow coincidence is not evidence.
    MissingDataReport shallow;
    shallow.missing = {"/sdcard/unrelated/readme.txt"};
    CHECK(InferDataMount(shallow, pack, "/sdcard").kind == DataMountHintKind::none);
    // Two shallow matches under the same mount do add up.
    shallow.missing.push_back("/sdcard/unrelated/data");
    const auto two = InferDataMount(shallow, pack, "/sdcard");
    CHECK(two.kind == DataMountHintKind::remount);
    CHECK(two.candidates.at(0).guest_dir == "/sdcard/unrelated");
}

TEST_CASE("ADR-0146 a game that ran gets no hint, whatever it missed") {
    TemporaryDirectory tree;
    const auto pack = Pack(tree.path);
    MissingDataReport report;
    report.missing = {"/sdcard/vendor/games/title/data/3d/camera/intro.bdae",
                      "/sdcard/vendor/games/title/data/menus/map.tga", "/sdcard/engine/content/a.bin"};
    report.failed = false;
    CHECK(InferDataMount(report, pack, "/sdcard").kind == DataMountHintKind::none);
    CHECK(InferDataMount(report, std::nullopt, "/sdcard").kind == DataMountHintKind::none);
    CHECK(InferDataMount(report, std::nullopt, "/sdcard").missing == 3);
    report.failed = true;
    CHECK(InferDataMount(report, pack, "/sdcard").kind == DataMountHintKind::remount);
    CHECK(InferDataMount(report, std::nullopt, "/sdcard").kind == DataMountHintKind::needs_data);
}

TEST_CASE("ADR-0146 never moves a pack the game already reads where it is") {
    TemporaryDirectory tree;
    const auto pack = Pack(tree.path);
    // A game that resolves textures next to their model first, then from its data root.
    MissingDataReport report;
    report.missing = {"/sdcard/vendor/title/data/menus/data/menus/map.tga",
                      "/sdcard/vendor/title/data/3d/camera/data/3d/camera/intro.bdae"};
    const auto probing = InferDataMount(report, pack, "/sdcard/Vendor/Title");
    CHECK(probing.kind == DataMountHintKind::remount);  // without what it found, the probes look like evidence
    report.found = {"/sdcard/vendor/title/data/menus/map.tga", "/sdcard/vendor/title/data/3d/camera/intro.bdae"};
    CHECK(InferDataMount(report, pack, "/sdcard/Vendor/Title").kind == DataMountHintKind::none);

    // Files the game made itself under the mount are not served by the pack.
    MissingDataReport misplaced;
    misplaced.missing = {"/sdcard/vendor/title/data/menus/map.tga", "/sdcard/vendor/title/data/3d/camera/intro.bdae"};
    misplaced.found = {"/sdcard/save.dat", "/sdcard/vendor/notes.txt"};
    const auto hint = InferDataMount(misplaced, pack, "/sdcard");
    CHECK(hint.kind == DataMountHintKind::remount);
    CHECK(hint.candidates.at(0).guest_dir == "/sdcard/vendor/title");
    // A pack that serves as much as a move would explain stays where it is.
    misplaced.found.push_back("/sdcard/data/menus/map.tga");
    CHECK(InferDataMount(misplaced, pack, "/sdcard").kind == DataMountHintKind::remount);
    misplaced.found.push_back("/sdcard/data/3d/camera/intro.bdae");
    CHECK(InferDataMount(misplaced, pack, "/sdcard").kind == DataMountHintKind::none);
}

TEST_CASE("ADR-0146 guesses the mount from where the picked directory sits") {
    TemporaryDirectory temporary;
    const auto root = temporary.path / "Downloads" / "Title";
    CHECK(GuessDataMount(root / "Android" / "data" / "com.example.game") == "/sdcard/Android/data/com.example.game");
    CHECK(GuessDataMount(root / "android" / "OBB" / "com.example.game") == "/sdcard/Android/obb/com.example.game");
    CHECK(GuessDataMount(root / "Android" / "data" / "com.example.game" / "files") ==
          "/sdcard/Android/data/com.example.game/files");
    CHECK(GuessDataMount(root / "Android") == "/sdcard/Android");
    CHECK(GuessDataMount(root / "sdcard" / "Vendor" / "Games" / "Title") == "/sdcard/Vendor/Games/Title");
    CHECK(GuessDataMount(root / "storage" / "emulated" / "0" / "Vendor") == "/sdcard/Vendor");
    // The innermost marker wins; a folder that merely happens to be called Android does not count.
    CHECK(GuessDataMount(root / "sdcard" / "Android" / "data" / "pkg") == "/sdcard/Android/data/pkg");
    CHECK(GuessDataMount(root / "Android" / "games" / "Title") == "/sdcard/Title");
    CHECK_FALSE(GuessDataMount(root / "sdcard"));
    // Without a marker the picked directory itself appears below /sdcard, unless it is an sdcard copy.
    CHECK(GuessDataMount(root) == "/sdcard/Title");
    std::filesystem::create_directories(root / "Backup" / "android" / "data");
    CHECK_FALSE(GuessDataMount(root / "Backup"));
    // A lone package-named directory: Android/obb when it holds .obb files, else Android/data.
    Write(root / "com.example.game" / "files" / "a.bin", "1");
    CHECK(GuessDataMount(root / "com.example.game") == "/sdcard/com.example.game");
    CHECK(GuessDataMount(root / "com.example.game", "com.example.game") == "/sdcard/Android/data/com.example.game");
    Write(root / "Com.Example.Obb" / "main.1.com.example.obb.OBB", "1");
    CHECK(GuessDataMount(root / "Com.Example.Obb", "com.example.obb") == "/sdcard/Android/obb/Com.Example.Obb");
}

TEST_CASE("ADR-0146 without a data pack only says the game may need one") {
    MissingDataReport report;
    report.missing = {"/sdcard/vendor/title/data/a.bin", "/sdcard/vendor/title/data/b/c.bin"};
    CHECK(InferDataMount(report, std::nullopt, "/sdcard").kind == DataMountHintKind::none);
    report.missing.push_back("/sdcard/Vendor/title/data/d.bin");
    const auto hint = InferDataMount(report, std::nullopt, "/sdcard");
    CHECK(hint.kind == DataMountHintKind::needs_data);
    CHECK(hint.search_root == "/sdcard/vendor/title/data");
    CHECK(hint.examples.size() == 3);
    CHECK(hint.candidates.empty());
}

TEST_CASE("ADR-0146 a missing expansion file alone shows the game needs its obb") {
    TemporaryDirectory tree;
    MissingDataReport report;
    report.missing = {"/sdcard/android/obb/org.example.game/main.7.org.example.game.obb",
                      "/sdcard/android/obb/org.example.game/patch.7.org.example.game.obb"};
    const auto hint = InferDataMount(report, std::nullopt, "/sdcard");
    CHECK(hint.kind == DataMountHintKind::needs_data);
    CHECK(hint.search_root == "/sdcard/android/obb/org.example.game");
    CHECK(hint.examples == report.missing);
    // The expansion file outweighs unrelated probes for where to look.
    report.missing.insert(report.missing.begin(), {"/sdcard/engine/a.txt", "/sdcard/engine/b.txt"});
    CHECK(InferDataMount(report, std::nullopt, "/sdcard").search_root == "/sdcard/android/obb/org.example.game");
    // Look-alike names are not expansion files.
    for (const auto* name : {"main.org.example.game.obb", "main.7.obb", "mainx.7.org.example.game.obb",
                             "main.7.org.example.game.obb.idx"}) {
        MissingDataReport other;
        other.missing = {std::string("/sdcard/android/obb/org.example.game/") + name};
        CHECK(InferDataMount(other, std::nullopt, "/sdcard").kind == DataMountHintKind::none);
    }

    // A picked pack holding only the main file explains it, though the match is a single shallow one.
    Write(tree.path / "Obb" / "Main.7.Org.Example.Game.obb", "1");
    report.missing.resize(4);
    const auto picked = InferDataMount(report, tree.path / "Obb", {});
    CHECK(picked.kind == DataMountHintKind::remount);
    REQUIRE(picked.candidates.size() == 1);
    CHECK(picked.candidates[0].guest_dir == "/sdcard/android/obb/org.example.game");
    CHECK(picked.candidates[0].explained == 1);
    // Not when the game already reads that file where the pack is mounted.
    report.found = {"/sdcard/android/obb/org.example.game/main.7.org.example.game.obb"};
    CHECK(InferDataMount(report, tree.path / "Obb", "/sdcard/Android/obb/org.example.game").kind ==
          DataMountHintKind::none);
}

TEST_CASE("ADR-0146 reads only well formed reports") {
    TemporaryDirectory tree;
    CHECK_THROWS_AS(static_cast<void>(LoadMissingDataReport(tree.path / "absent.json")), LibraryError);
    // Schema 1 predates the run end: its misses cannot tell a probe from a failure.
    for (
        const auto* text :
        {"{", R"({"schema":1,"external_dir":null,"external_guest_dir":"/sdcard","missing":[],"truncated":false})",
         R"({"schema":2,"end":"fault","external_dir":null,"external_guest_dir":"/sdcard","missing":["/data/x"],"truncated":false,"found":[]})",
         R"({"schema":2,"end":"fault","external_dir":null,"external_guest_dir":"/sdcard","missing":[],"truncated":false,"found":["/data/x"]})",
         R"({"schema":2,"end":"bored","external_dir":null,"external_guest_dir":"/sdcard","missing":[],"truncated":false,"found":[]})",
         R"({"schema":2,"external_dir":null,"external_guest_dir":"/sdcard","missing":[],"truncated":false,"found":[]})",
         R"({"schema":2,"end":"fault","external_guest_dir":"/sdcard","missing":[],"truncated":false,"found":[]})"}) {
        Write(tree.path / "bad.json", text);
        CHECK_THROWS_AS(static_cast<void>(LoadMissingDataReport(tree.path / "bad.json")), LibraryError);
    }
    Write(
        tree.path / "ok.json",
        R"({"schema":2,"end":"crashed","external_dir":"/pack","external_guest_dir":"/sdcard/g","missing":["/sdcard/g/x"],"truncated":true,"found":["/sdcard/g/y"]})");
    const auto report = LoadMissingDataReport(tree.path / "ok.json");
    CHECK(report.external_dir == std::filesystem::path("/pack"));
    CHECK(report.external_guest_dir == "/sdcard/g");
    CHECK(report.missing == std::vector<std::string>{"/sdcard/g/x"});
    CHECK(report.found == std::vector<std::string>{"/sdcard/g/y"});
    CHECK(report.truncated);
    CHECK(report.failed);
    for (const auto* end : {"startup_failed", "fault", "no_frame"}) {
        Write(tree.path / "end.json",
              std::string(R"({"schema":2,"end":")") + end +
                  R"(","external_dir":null,"external_guest_dir":"/sdcard","missing":[],"truncated":false,"found":[]})");
        CHECK(LoadMissingDataReport(tree.path / "end.json").failed);
    }
    for (const auto* end : {"closed", "exited"}) {
        Write(tree.path / "end.json",
              std::string(R"({"schema":2,"end":")") + end +
                  R"(","external_dir":null,"external_guest_dir":"/sdcard","missing":[],"truncated":false,"found":[]})");
        CHECK_FALSE(LoadMissingDataReport(tree.path / "end.json").failed);
    }
}

TEST_CASE("ADR-0146 GUI suggests and applies a mount for the current configuration only") {
    using namespace ogplay;
    TemporaryDirectory tree;
    const auto pack = Pack(tree.path);
    Write(tree.path / "game.apk", "apk");
    LibraryStore store(tree.path / "library");
    LibraryMetadata metadata{.package = "org.example.game", .display_name = "Game", .version_code = 1,
                             .imported_at = "2026-10-20T00:00:00Z", .external_dir = pack};
    const auto id = store.Import({.source_apk = tree.path / "game.apk", .metadata = metadata}, true);
    const auto entry = store.EntriesRoot() / id;
    const auto report = [&](std::string_view guest, bool data, std::string_view end = "fault") {
        Write(
            entry / "last-run-missing.json",
            std::string(R"({"schema":2,"end":")") + std::string(end) + R"(","external_dir":)" +
                (data ? "\"/x\"" : "null") + R"(,"external_guest_dir":")" + std::string(guest) +
                R"(","missing":["/sdcard/vendor/title/data/menus/map.tga","/sdcard/vendor/title/data/3d/camera/intro.bdae"],"truncated":false,"found":[]})");
    };
    frontend::GuiRpcService rpc(store, tree.path / "ogplay.exe", {
        [](const auto&) { return frontend::LibraryViewContext{}; }, [](const auto&) {}, [](const auto&) {}, {}, {},
        [] { return std::string("2026-10-20T00:00:00Z"); }});
    const auto call = [&](std::string_view method, const std::string& params) {
        app::JsonParseError error;
        auto document = app::JsonDocument::ParseStrict(rpc.Handle(
            std::string(R"({"jsonrpc":"2.0","id":1,"method":")") + std::string(method) + R"(","params":)" + params + "}"), error);
        REQUIRE(document);
        return std::move(*document);
    };
    const auto kind = [&](const std::string& params) {
        const auto response = call("library.data_hint", params);
        REQUIRE(response.Root().Member("result"));
        return std::string(*response.Root().Member("result")->Member("kind")->String());
    };
    const auto params = R"({"installation_id":")" + id + "\"}";
    CHECK(kind(params) == "none");  // no report yet
    report("/sdcard/elsewhere", true);
    CHECK(kind(params) == "none");  // the report describes another mount
    report("/sdcard", true, "closed");
    CHECK(kind(params) == "none");  // the game ran, so its misses are not evidence
    report("/sdcard", true);
    const auto hint = call("library.data_hint", params);
    const auto result = hint.Root().Member("result");
    REQUIRE(result);
    CHECK(result->Member("kind")->String() == "remount");
    CHECK(result->Member("candidates")->Element(0)->Member("guest_dir")->String() == "/sdcard/vendor/title");
    CHECK(SuggestDataMount(store, id).hint.candidates.at(0).explained == 2);

    SaveGameSettings(entry, {{{"data_mount_hint", false}}});
    CHECK(kind(params) == "none");
    SaveGameSettings(entry, {});

    CHECK(call("library.data_mount.apply", R"({"installation_id":")" + id + R"(","external_guest_dir":"/data"})")
              .Root().Member("error"));
    const auto applied = call("library.data_mount.apply", R"({"installation_id":")" + id + R"(","external_guest_dir":"/sdcard/vendor/title"})");
    REQUIRE(applied.Root().Member("result"));
    const auto settings = LoadGameSettings(entry);
    CHECK(std::get<std::string>(settings.values.at("external_guest_dir")) == "/sdcard/vendor/title");
    CHECK(kind(params) == "none");  // the old report no longer matches the mount

    // An entry without data: the hint only asks for a directory, then judges the pick.
    LibraryMetadata bare = metadata;
    bare.external_dir.reset();
    const auto bare_id = store.Import({.source_apk = tree.path / "game.apk", .metadata = bare}, true);
    Write(
        store.EntriesRoot() / bare_id / "last-run-missing.json",
        R"({"schema":2,"end":"no_frame","external_dir":null,"external_guest_dir":"/sdcard","missing":["/sdcard/vendor/title/data/menus/map.tga","/sdcard/vendor/title/data/3d/camera/intro.bdae","/sdcard/vendor/title/data/x.bin"],"truncated":false,"found":[]})");
    CHECK(kind(R"({"installation_id":")" + bare_id + "\"}") == "needs_data");
    CHECK(kind(R"({"installation_id":")" + bare_id + R"(","external_dir":")" + pack.generic_string() + "\"}") == "remount");
}
