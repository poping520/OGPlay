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

TEST_CASE("ADR-0146 reads only well formed reports") {
    TemporaryDirectory tree;
    CHECK_THROWS_AS(static_cast<void>(LoadMissingDataReport(tree.path / "absent.json")), LibraryError);
    for (const auto* text : {"{", R"({"schema":2,"external_dir":null,"external_guest_dir":"/sdcard","missing":[],"truncated":false})",
                             R"({"schema":1,"external_dir":null,"external_guest_dir":"/sdcard","missing":["/data/x"],"truncated":false})",
                             R"({"schema":1,"external_guest_dir":"/sdcard","missing":[],"truncated":false})"}) {
        Write(tree.path / "bad.json", text);
        CHECK_THROWS_AS(static_cast<void>(LoadMissingDataReport(tree.path / "bad.json")), LibraryError);
    }
    Write(tree.path / "ok.json", R"({"schema":1,"external_dir":"/pack","external_guest_dir":"/sdcard/g","missing":["/sdcard/g/x"],"truncated":true})");
    const auto report = LoadMissingDataReport(tree.path / "ok.json");
    CHECK(report.external_dir == std::filesystem::path("/pack"));
    CHECK(report.external_guest_dir == "/sdcard/g");
    CHECK(report.missing == std::vector<std::string>{"/sdcard/g/x"});
    CHECK(report.truncated);
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
    const auto report = [&](std::string_view guest, bool data) {
        Write(entry / "last-run-missing.json", std::string(R"({"schema":1,"external_dir":)") + (data ? "\"/x\"" : "null") +
              R"(,"external_guest_dir":")" + std::string(guest) +
              R"(","missing":["/sdcard/vendor/title/data/menus/map.tga","/sdcard/vendor/title/data/3d/camera/intro.bdae"],"truncated":false})");
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
    Write(store.EntriesRoot() / bare_id / "last-run-missing.json",
          R"({"schema":1,"external_dir":null,"external_guest_dir":"/sdcard","missing":["/sdcard/vendor/title/data/menus/map.tga","/sdcard/vendor/title/data/3d/camera/intro.bdae","/sdcard/vendor/title/data/x.bin"],"truncated":false})");
    CHECK(kind(R"({"installation_id":")" + bare_id + "\"}") == "needs_data");
    CHECK(kind(R"({"installation_id":")" + bare_id + R"(","external_dir":")" + pack.generic_string() + "\"}") == "remount");
}
