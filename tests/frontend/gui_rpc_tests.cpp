#include <algorithm>
#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <thread>
#include "ogplay/app/encoding.h"
#include "frontend/gui/rpc.h"
#include "frontend/gui/rpc_error.h"
#include "ogplay/app/library.h"

namespace {
using namespace ogplay;
using namespace ogplay::app;

struct Fixture {
    static inline std::atomic<unsigned> sequence{};
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("ogplay-gui-rpc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
         std::to_string(sequence++));
    ogplay::app::LibraryStore store{root};
    frontend::LibraryViewContext context;
    unsigned launches{}, opens{}, reads{}, minimized{};
    std::vector<frontend::GuiDashboard> dashboards;
    std::string opened_dashboard;
    std::string opened_editor;
    bool fail_launch{};
    bool fail_template_launch{};
    std::optional<frontend::LaunchPlan> plan;
    std::filesystem::path opened;
    std::vector<std::string> demo_packages;
    unsigned demo_seeds{};
    std::vector<UiLanguage> language_changes;
    frontend::GuiRpcService rpc{
        store,
        root / "ogplay.exe",
        {[this](const auto&) {
             ++reads;
             return context;
         },
         [this](const auto& value) {
             if (fail_template_launch)
                 throw frontend::GuiTextError("图标素材缺失：{path}", {{"path", "/missing/icon.png"}});
             if (fail_launch) throw std::runtime_error("spawn failed");
             ++launches;
             plan = value;
         },
         [this](const auto& path) {
             ++opens;
             opened = path;
         },
         [](const auto& path) {
             ogplay::app::ApkImportAnalysis result;
             result.source_apk = path;
             result.display_name = "导入游戏";
             result.manifest.package = "org.example.game";
             result.manifest.version_code = 0;
             return result;
         },
         [](bool) {
             return std::async(std::launch::deferred,
                               []() -> std::optional<std::filesystem::path> { return std::nullopt; });
         },
         [] { return std::string("2026-09-22T00:00:00Z"); },
         [this] { ++minimized; },
         {},
         {},
         [] { return frontend::LoadCredits(std::filesystem::path(OGPLAY_SOURCE_DIR) / "data"); },
         [](std::string_view id) {
             return frontend::ReadCreditNotices(std::filesystem::path(OGPLAY_SOURCE_DIR) / "data", id);
         },
         [this] { return dashboards; },
         [this](std::string_view id) { opened_dashboard = id; },
         [this](std::string_view id) { opened_editor = id; },
         [this] { return demo_packages; },
         [this] { ++demo_seeds; },
         [this](UiLanguage language) { language_changes.push_back(language); }}};

    Fixture() {
        std::filesystem::create_directories(root);
        std::ofstream(root / "ogplay.exe") << "fixture";
        const auto entry = root / "library/org.example.game";
        std::filesystem::create_directories(entry);
        std::ofstream(entry / "game.apk") << "fixture";
        std::ofstream(entry / "meta.toml")
            << "schema = 1\npackage = \"org.example.game\"\ndisplay_name = \"游戏 <script>\"\n"
               "version_code = 1\nversion_name = \"1.0\"\nimported_at = \"now\"\n";
        const auto profiles = root / "profiles";
        std::filesystem::create_directories(profiles);
        ogplay::app::SaveLibraryConfig(root, {.profiles_dir = profiles});
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

app::JsonDocument Decode(const std::string& response) {
    app::JsonParseError error;
    auto result = app::JsonDocument::ParseStrict(response, error);
    REQUIRE(result.has_value());
    return std::move(*result);
}
}  // namespace

TEST_CASE("GUI RPC rejects unknown schema before host operations") {
    Fixture fixture;
    for (
        const auto* request :
        {R"({"jsonrpc":"2.0","id":1,"method":"library.launch","params":{"installation_id":"org.example.game","argv":[]}})",
         R"({"jsonrpc":"2.0","id":1,"method":"library.list","extra":true})",
         R"({"jsonrpc":"2.0","id":1,"method":"library.list","params":{"extra":true}})",
         R"({"jsonrpc":"2.0","id":1,"method":"library.launch","params":{}})",
         R"({"jsonrpc":"2.0","id":1,"method":"library.open_dir","params":{"installation_id":"org.example.game","kind":"arbitrary"}})",
         R"({"jsonrpc":"2.0","id":1,"method":"library.list","params":[]})",
         R"({"jsonrpc":"2.0","id":1,"method":"library.list","method":"library.launch"})",
         R"({"jsonrpc":"2.0","id":1,"method":"runtime.execute"})", "{"}) {
        const auto result = Decode(fixture.rpc.Handle(request));
        CHECK(result.Root().Member("error").has_value());
    }
    CHECK(fixture.reads == 0);
    CHECK(fixture.launches == 0);
    CHECK(fixture.opens == 0);
}

TEST_CASE("GUI settings RPC applies validated patches and rejects stale writes") {
    Fixture fixture;
    const auto get = [&] { return Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"settings.get"})")); };
    auto initial = get();
    const auto result = initial.Root().Member("result");
    REQUIRE(result.has_value());
    CHECK(result->Member("schema")->UnsignedInteger() == 2);
    // Release builds hide reserved rows; the field list itself stays complete.
#ifdef NDEBUG
    CHECK(result->Member("reserved_visible")->Bool() == std::optional<bool>{false});
#else
    CHECK(result->Member("reserved_visible")->Bool() == std::optional<bool>{true});
#endif
    CHECK(result->Member("fields")->Size() == ogplay::app::SettingDefinitions().size());
    // Unpolished settings stay in the list, flagged hidden, so their values still validate and save.
    std::vector<std::string> hidden;
    for (std::size_t index = 0; index < result->Member("fields")->Size(); ++index) {
        const auto field = result->Member("fields")->Element(index);
        if (field->Member("hidden")->Bool() == std::optional<bool>{true})
            hidden.emplace_back(*field->Member("key")->String());
    }
    CHECK(hidden == std::vector<std::string>{"cartridge", "supersample", "dashboard_auto_open"});
    const auto revision = std::string(*result->Member("revision")->String());
    const auto update = [&](std::string_view token, std::string_view key, auto value) {
        app::JsonWriter writer;
        const auto request = writer.Object(), params = writer.Object(), values = writer.Object();
        writer.AddString(request, "jsonrpc", "2.0");
        writer.AddUnsignedInteger(request, "id", 2);
        writer.AddString(request, "method", "settings.set");
        writer.AddString(params, "revision", token);
        if constexpr (std::is_same_v<decltype(value), unsigned>)
            writer.AddUnsignedInteger(values, key, value);
        else
            writer.AddString(values, key, value);
        writer.Add(params, "values", values);
        writer.Add(request, "params", params);
        return Decode(fixture.rpc.Handle(writer.Serialize(request)));
    };
    CHECK(update(revision, "theme", "light").Root().Member("result").has_value());
    auto saved = ogplay::app::LoadLibraryConfig(fixture.root);
    CHECK(saved.profiles_dir == fixture.root / "profiles");
    CHECK(std::get<std::string>(ogplay::app::Setting(saved, "theme")) == "light");
    CHECK(update(revision, "theme", "dark").Root().Member("error").has_value());
    auto current = get();
    const auto token = std::string(*current.Root().Member("result")->Member("revision")->String());
    CHECK(update(token, "supersample", 5u).Root().Member("error").has_value());
    CHECK(update(token, "supersample", "2").Root().Member("error").has_value());
    CHECK(update(token, "unknown", "value").Root().Member("error").has_value());
    CHECK(
        update(token, "profiles_dir", (fixture.root / "missing").generic_string()).Root().Member("error").has_value());
    CHECK(ogplay::app::LoadLibraryConfig(fixture.root) == saved);
    CHECK(Decode(fixture.rpc.Handle(
                     R"({"jsonrpc":"2.0","id":3,"method":"settings.open_dir","params":{"kind":"library"}})"))
              .Root()
              .Member("result")
              .has_value());
    CHECK(fixture.opened == fixture.root);
    CHECK(
        Decode(fixture.rpc.Handle(
                   R"({"jsonrpc":"2.0","id":3,"method":"settings.open_dir","params":{"kind":"library","path":"C:/"}})"))
            .Root()
            .Member("error")
            .has_value());
    CHECK(fixture.opens == 1);
    const auto preview = Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":4,"method":"settings.icon_preview"})"));
    for (const auto* style : {"cradle", "badge"})
        CHECK(preview.Root().Member("result")->Member(style)->String()->starts_with("data:image/png;base64,"));
    CHECK(Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":5,"method":"settings.icon_preview","params":{"x":1}})"))
              .Root()
              .Member("error")
              .has_value());
    app::JsonWriter writer;
    const auto analyze = writer.Object(), params = writer.Object();
    writer.AddString(analyze, "jsonrpc", "2.0");
    writer.AddUnsignedInteger(analyze, "id", 4);
    writer.AddString(analyze, "method", "library.analyze");
    writer.AddString(params, "path", (fixture.root / "library/org.example.game/game.apk").generic_string());
    writer.Add(analyze, "params", params);
    REQUIRE(Decode(fixture.rpc.Handle(writer.Serialize(analyze))).Root().Member("result"));
    CHECK(update(token, "theme", "dark").Root().Member("error").has_value());
    CHECK(ogplay::app::LoadLibraryConfig(fixture.root) == saved);
    std::ofstream(fixture.root / "config.toml", std::ios::binary) << "schema = 99\n";
    CHECK(get().Root().Member("error").has_value());
    CHECK(update(token, "theme", "dark").Root().Member("error").has_value());
    CHECK(std::filesystem::file_size(fixture.root / "config.toml") == 12);
}

TEST_CASE("GUI minimizes only after a successful launch when requested") {
    Fixture fixture;
    const auto* launch =
        R"({"jsonrpc":"2.0","id":1,"method":"library.launch","params":{"installation_id":"org.example.game"}})";
    REQUIRE(Decode(fixture.rpc.Handle(launch)).Root().Member("result"));
    CHECK(fixture.minimized == 0);
    auto config = ogplay::app::LoadLibraryConfig(fixture.root);
    ogplay::app::SetSetting(config, "minimize_on_launch", true);
    ogplay::app::SaveLibraryConfig(fixture.root, config);
    fixture.fail_launch = true;
    CHECK(Decode(fixture.rpc.Handle(launch)).Root().Member("error").has_value());
    CHECK(fixture.minimized == 0);
    fixture.fail_launch = false;
    CHECK(Decode(fixture.rpc.Handle(launch)).Root().Member("result").has_value());
    CHECK(fixture.minimized == 1);
}

TEST_CASE("GUI instance settings RPC isolates instances and uses the same preview and launch plan") {
    Fixture fixture;
    const auto first = fixture.root / "library/org.example.game", second = fixture.root / "library/org.example.game-2";
    std::filesystem::copy(first, second, std::filesystem::copy_options::recursive);
    const auto get = [&](std::string_view id) {
        app::JsonWriter writer;
        const auto request = writer.Object(), params = writer.Object();
        writer.AddString(request, "jsonrpc", "2.0");
        writer.AddUnsignedInteger(request, "id", 1);
        writer.AddString(request, "method", "game_settings.get");
        writer.AddString(params, "installation_id", id);
        writer.Add(request, "params", params);
        return Decode(fixture.rpc.Handle(writer.Serialize(request)));
    };
    const auto set = [&](std::string_view revision, std::string_view values) {
        auto parsed = Decode(std::string(values));
        app::JsonWriter writer;
        const auto request = writer.Object(), params = writer.Object();
        writer.AddString(request, "jsonrpc", "2.0");
        writer.AddUnsignedInteger(request, "id", 2);
        writer.AddString(request, "method", "game_settings.set");
        writer.AddString(params, "installation_id", "org.example.game");
        writer.AddString(params, "revision", revision);
        writer.Add(params, "values", writer.Copy(parsed.Root()));
        writer.Add(request, "params", params);
        return Decode(fixture.rpc.Handle(writer.Serialize(request)));
    };
    auto initial = get("org.example.game");
    REQUIRE(initial.Root().Member("result"));
    CHECK(initial.Root().Member("result")->Member("reserved_visible")->Bool().has_value());
    std::vector<std::string> hidden;
    const auto fields = initial.Root().Member("result")->Member("fields");
    for (std::size_t index = 0; index < fields->Size(); ++index)
        if (fields->Element(index)->Member("hidden")->Bool() == std::optional<bool>{true})
            hidden.emplace_back(*fields->Element(index)->Member("key")->String());
    CHECK(hidden == std::vector<std::string>{"supersample"});  // still saved and launched, as below
    const auto revision = std::string(*initial.Root().Member("result")->Member("revision")->String());
    for (const auto values : {R"({"supersample":5})", R"({"unknown":true})", R"({"supersample":"2"})",
                              R"({"mcp_manual_step":true})", R"({"mute":null})", R"({"external_dir":"relative"})",
                              R"({"external_guest_dir":"/sdcard/../data"})", R"({"external_guest_dir":"/system"})"})
        CHECK(set(revision, values).Root().Member("error").has_value());
    CHECK_FALSE(std::filesystem::exists(first / "settings.toml"));
    auto changed =
        set(revision, R"({"supersample":3,"mcp_enabled":true,"mcp_manual_step":true,"ephemeral_sandbox":true})");
    REQUIRE(changed.Root().Member("result"));
    CHECK(ogplay::app::LoadGameSettings(second).values.empty());
    CHECK(set(revision, "{}").Root().Member("error").has_value());
    const auto preview = changed.Root().Member("result")->Member("previews")->Member("normal")->Member("argv");
    REQUIRE(
        Decode(
            fixture.rpc.Handle(
                R"({"jsonrpc":"2.0","id":3,"method":"library.launch","params":{"installation_id":"org.example.game"}})"))
            .Root()
            .Member("result"));
    REQUIRE(fixture.plan);
    REQUIRE(preview->Size() == fixture.plan->argv.size());
    for (std::size_t i = 0; i < preview->Size(); ++i)
        CHECK(preview->Element(i)->String() == fixture.plan->argv[i]);
    const auto current = std::string(*changed.Root().Member("result")->Member("revision")->String());
    auto global = ogplay::app::LoadLibraryConfig(fixture.root);
    ogplay::app::SetSetting(global, "supersample", std::uint32_t{4});
    ogplay::app::SaveLibraryConfig(fixture.root, global);
    CHECK(set(current, "{}").Root().Member("error").has_value());
    auto reload = get("org.example.game");
    const auto fresh = std::string(*reload.Root().Member("result")->Member("revision")->String());
    REQUIRE(set(fresh, "{}").Root().Member("result"));
    CHECK(ogplay::app::LoadGameSettings(first).values.empty());
    CHECK(get("../org.example.game").Root().Member("error").has_value());
    std::ofstream(first / "settings.toml") << "schema = 99\n";
    CHECK(get("org.example.game").Root().Member("error").has_value());
    REQUIRE(get("org.example.game-2").Root().Member("result"));
    CHECK(set(fresh, "{}").Root().Member("error").has_value());
}

TEST_CASE("GUI game settings save past a linked pack that went away but reject a new missing one") {
    Fixture fixture;
    const auto directory = fixture.root / "library/org.example.game";
    const auto pack = (fixture.root / "pack").generic_string();
    std::filesystem::create_directories(pack);
    ogplay::app::SaveGameSettings(directory, {{{"external_dir", pack}}});
    std::filesystem::remove(pack);
    const auto save = [&](std::string_view values) {
        const auto got = Decode(fixture.rpc.Handle(
            R"({"jsonrpc":"2.0","id":1,"method":"game_settings.get","params":{"installation_id":"org.example.game"}})"));
        app::JsonWriter writer;
        const auto parsed = Decode(std::string(values));
        const auto request = writer.Object(), params = writer.Object();
        writer.AddString(request, "jsonrpc", "2.0");
        writer.AddUnsignedInteger(request, "id", 2);
        writer.AddString(request, "method", "game_settings.set");
        writer.AddString(params, "installation_id", "org.example.game");
        writer.AddString(params, "revision", *got.Root().Member("result")->Member("revision")->String());
        writer.Add(params, "values", writer.Copy(parsed.Root()));
        writer.Add(request, "params", params);
        return Decode(fixture.rpc.Handle(writer.Serialize(request)));
    };
    CHECK(save(R"({"external_dir":")" + pack + R"(","supersample":2})").Root().Member("result"));
    CHECK(save(R"({"external_dir":")" + pack + R"(-other"})").Root().Member("error"));
}

TEST_CASE("GUI library facts use instance external directory and isolate broken settings") {
    Fixture fixture;
    const auto directory = fixture.root / "library/org.example.game";
    std::ofstream(directory / "meta.toml", std::ios::app) << "profile_id = \"org.example.game\"\n";
    const auto list = [&] { return Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"library.list"})")); };
    auto before = list();
    CHECK(before.Root().Member("result")->Member("items")->Element(0)->Member("status")->String() == "ready");
    const auto external = fixture.root / "external";
    std::filesystem::create_directories(external);
    ogplay::app::SaveGameSettings(directory, {{{"external_dir", external.generic_string()}}});
    auto after = list();
    CHECK(after.Root().Member("result")->Member("items")->Element(0)->Member("status")->String() == "ready");
    REQUIRE(
        Decode(
            fixture.rpc.Handle(
                R"({"jsonrpc":"2.0","id":2,"method":"library.open_dir","params":{"installation_id":"org.example.game","kind":"external"}})"))
            .Root()
            .Member("result"));
    CHECK(fixture.opened == external);
    std::filesystem::copy(directory, fixture.root / "library/org.example.game-2",
                          std::filesystem::copy_options::recursive);
    fixture.context.profile_errors["org.example.game"] = "bad override";
    auto unavailable = list();
    const auto items = unavailable.Root().Member("result")->Member("items");
    REQUIRE(items->Size() == 2);
    CHECK(items->Element(0)->Member("status")->String() == "profile_catalog_unavailable");
    CHECK(items->Element(1)->Member("can_launch")->Bool() == true);
    std::ofstream(directory / "settings.toml") << "schema = 99\n";
    auto damaged = list();
    CHECK(damaged.Root().Member("result")->Member("items")->Element(0)->Member("status")->String() == "damaged");
    CHECK(damaged.Root().Member("result")->Member("items")->Element(1)->Member("can_launch")->Bool() == true);
}

TEST_CASE("GUI RPC serializes model facts and enforces launch eligibility") {
    Fixture fixture;
    auto list = Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":"list","method":"library.list"})"));
    CHECK(list.Root().Member("id")->String() == "list");
    const auto items = list.Root().Member("result")->Member("items");
    REQUIRE(items->Size() == 1);
    CHECK(items->Element(0)->Member("status")->String() == "ready");
    CHECK(items->Element(0)->Member("profile")->IsNull());
    CHECK(items->Element(0)->Member("display_name")->String() == "游戏 <script>");
    CHECK(items->Element(0)->Member("can_launch")->Bool() == true);
    CHECK(items->Element(0)->Member("version_name")->String() == "1.0");
    CHECK(items->Element(0)->Member("version_code")->UnsignedInteger() == 1);
    CHECK(items->Element(0)->Member("imported_at")->String() == "now");
    CHECK(items->Element(0)->Member("sandbox_path")->String()->ends_with("/sandbox/org.example.game"));
    CHECK(items->Element(0)->Member("log_directory")->String()->ends_with("/library/org.example.game"));
    // Facts only: codes, ids and paths; the launcher writes every sentence.
    CHECK(items->Element(0)->Member("damage_reason")->IsNull());
    CHECK_FALSE(items->Element(0)->Member("detail").has_value());
    CHECK_FALSE(items->Element(0)->Member("version").has_value());
    CHECK(items->Element(0)->Member("external")->Member("status")->String() == "not_required");
    CHECK(items->Element(0)->Member("external")->Member("value")->String() == "");
    CHECK(items->Element(0)->Member("external")->Member("owned")->Bool() == false);
    const auto* launch =
        R"({"jsonrpc":"2.0","id":2,"method":"library.launch","params":{"installation_id":"org.example.game"}})";
    auto started = Decode(fixture.rpc.Handle(launch));
    CHECK(started.Root().Member("result").has_value());
    REQUIRE(fixture.plan.has_value());
    const auto id = std::find(fixture.plan->argv.begin(), fixture.plan->argv.end(), "--installation-id");
    REQUIRE(id + 1 < fixture.plan->argv.end());
    CHECK(*(id + 1) == "org.example.game");
    CHECK(fixture.plan->log_path == fixture.root / "library/org.example.game/last-run.log");
    fixture.context.running_packages = {"org.example.game"};
    CHECK(Decode(fixture.rpc.Handle(launch)).Root().Member("error").has_value());
    fixture.context.running_packages.clear();
    fixture.context.profile_catalog_error = "unavailable";
    CHECK(Decode(fixture.rpc.Handle(launch)).Root().Member("error").has_value());
    CHECK(fixture.launches == 1);
}

TEST_CASE("GUI about RPC lists the shipped open-source projects and reads each license on request") {
    Fixture fixture;
    auto list = Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"about.credits"})"));
    const auto projects = list.Root().Member("result")->Member("projects");
    REQUIRE(projects);
    REQUIRE(projects->Size() > 10);
    std::vector<std::string> ids;
    std::map<std::string, std::string> parents;
    for (std::size_t index = 0; index < projects->Size(); ++index) {
        ids.emplace_back(*projects->Element(index)->Member("id")->String());
        if (const auto parent = projects->Element(index)->Member("parent")) parents[ids.back()] = *parent->String();
    }
    // Only what OGPlay gets through another project nests; its own dependencies stay top level.
    CHECK(parents.at("oaknut") == "dynarmic");
    CHECK(parents.at("zycore") == "zydis");
    CHECK(parents.at("draco") == "three");
    CHECK(parents.at("zlib") == "aosp");
    for (const auto* id : {"dynarmic", "boost", "three", "aosp"})
        CHECK_FALSE(parents.contains(id));
    for (const auto* id : {"sdl", "angle", "dynarmic", "ffmpeg", "preact", "aosp"})
        CHECK(std::find(ids.begin(), ids.end(), id) != ids.end());
    // Every listed project has readable, non-empty texts; that is the whole point of the page.
    for (const auto& id : ids) {
        auto notices = Decode(
            fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":2,"method":"about.notices","params":{"id":")" + id + R"("}})"));
        const auto texts = notices.Root().Member("result")->Member("notices");
        REQUIRE_MESSAGE(texts, id);
        REQUIRE(texts->Size() > 0);
        for (std::size_t index = 0; index < texts->Size(); ++index)
            CHECK_FALSE(texts->Element(index)->Member("text")->String()->empty());
    }
    CHECK(Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":3,"method":"about.notices","params":{"id":"nope"}})"))
              .Root()
              .Member("error"));
    CHECK(Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":4,"method":"about.credits","params":{"x":1}})"))
              .Root()
              .Member("error"));
}

TEST_CASE("GUI RPC opens only an existing instance directory") {
    Fixture fixture;
    const auto* log =
        R"({"jsonrpc":"2.0","id":1,"method":"library.open_dir","params":{"installation_id":"org.example.game","kind":"log"}})";
    CHECK(Decode(fixture.rpc.Handle(log)).Root().Member("result").has_value());
    CHECK(fixture.opened == fixture.root / "library/org.example.game");
    const auto* sandbox =
        R"({"jsonrpc":"2.0","id":1,"method":"library.open_dir","params":{"installation_id":"org.example.game","kind":"sandbox"}})";
    CHECK(Decode(fixture.rpc.Handle(sandbox)).Root().Member("error").has_value());
    CHECK_FALSE(std::filesystem::exists(fixture.root / "sandbox/org.example.game"));
    const auto traversal = Decode(fixture.rpc.Handle(
        R"({"jsonrpc":"2.0","id":1,"method":"library.open_dir","params":{"installation_id":"../../","kind":"log"}})"));
    CHECK(traversal.Root().Member("error")->Member("code")->Integer() == -32004);
    CHECK(fixture.opens == 1);
}

TEST_CASE("GUI RPC measures APK, data pack and game data storage on request") {
    Fixture fixture;
    const auto directory = fixture.root / "library/org.example.game";
    const auto storage = [&] {
        return Decode(fixture.rpc.Handle(
            R"({"jsonrpc":"2.0","id":1,"method":"library.storage","params":{"installation_id":"org.example.game"}})"));
    };
    auto before = storage();
    auto result = before.Root().Member("result");
    REQUIRE(result);
    CHECK(result->Member("apk_bytes")->Integer() ==
          static_cast<std::int64_t>(std::filesystem::file_size(directory / "game.apk")));
    // No data pack configured and the game has not run yet: nothing to measure.
    CHECK(result->Member("external_bytes")->IsNull());
    CHECK(result->Member("external_guest_dir")->IsNull());
    CHECK(result->Member("sandbox_bytes")->IsNull());

    const auto external = fixture.root / "external";
    std::filesystem::create_directories(external / "nested");
    std::ofstream(external / "a.bin", std::ios::binary) << std::string(100, 'a');
    std::ofstream(external / "nested/b.bin", std::ios::binary) << std::string(23, 'b');
    ogplay::app::SaveGameSettings(directory, {{{"external_dir", external.generic_string()},
                                               {"external_guest_dir", std::string("/sdcard/Android/obb")}}});
    const auto sandbox = fixture.root / "sandbox/org.example.game";
    std::filesystem::create_directories(sandbox);
    std::ofstream(sandbox / "save.dat", std::ios::binary) << std::string(7, 's');
    auto after = storage();
    result = after.Root().Member("result");
    REQUIRE(result);
    CHECK(result->Member("external_bytes")->Integer() == 123);
    CHECK(result->Member("external_guest_dir")->String() == "/sdcard/Android/obb");
    CHECK(result->Member("sandbox_bytes")->Integer() == 7);
    CHECK(Decode(fixture.rpc.Handle(
                     R"({"jsonrpc":"2.0","id":1,"method":"library.storage","params":{"installation_id":"nope"}})"))
              .Root()
              .Member("error"));
    CHECK(
        Decode(
            fixture.rpc.Handle(
                R"({"jsonrpc":"2.0","id":1,"method":"library.storage","params":{"installation_id":"org.example.game","x":1}})"))
            .Root()
            .Member("error"));
}

TEST_CASE("GUI RPC hides demo games with the setting, and removing one turns demos off") {
    Fixture fixture;
    fixture.demo_packages = {"org.example.game"};
    const auto list = [&] { return Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"library.list"})")); };
    const auto shown = [&] {
        return std::get<bool>(ogplay::app::Setting(ogplay::app::LoadLibraryConfig(fixture.root), "show_demo_games"));
    };
    auto visible = list();
    REQUIRE(visible.Root().Member("result")->Member("items")->Size() == 1);
    CHECK(visible.Root().Member("result")->Member("items")->Element(0)->Member("builtin_demo")->Bool() == true);

    auto config = ogplay::app::LoadLibraryConfig(fixture.root);
    ogplay::app::SetSetting(config, "show_demo_games", false);
    ogplay::app::SaveLibraryConfig(fixture.root, config);
    CHECK(list().Root().Member("result")->Member("items")->Size() == 0);
    // Hidden only from the list: the entry and its directories stay.
    CHECK(std::filesystem::exists(fixture.root / "library/org.example.game/meta.toml"));

    // Turning demos back on restores missing demos through the host.
    const auto settings = Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":2,"method":"settings.get"})"));
    const auto revision = std::string(*settings.Root().Member("result")->Member("revision")->String());
    const auto set = [&](const std::string& rev, bool value) {
        app::JsonWriter writer;
        const auto request = writer.Object(), params = writer.Object(), values = writer.Object();
        writer.AddString(request, "jsonrpc", "2.0");
        writer.AddUnsignedInteger(request, "id", 3);
        writer.AddString(request, "method", "settings.set");
        writer.AddString(params, "revision", rev);
        writer.AddBool(values, "show_demo_games", value);
        writer.Add(params, "values", values);
        writer.Add(request, "params", params);
        return Decode(fixture.rpc.Handle(writer.Serialize(request)));
    };
    const auto on = set(revision, true);
    REQUIRE(on.Root().Member("result"));
    CHECK(fixture.demo_seeds == 1);
    CHECK(list().Root().Member("result")->Member("items")->Size() == 1);
    // Saving with demos already on does not seed again.
    REQUIRE(set(std::string(*on.Root().Member("result")->Member("revision")->String()), true).Root().Member("result"));
    CHECK(fixture.demo_seeds == 1);

    const auto removed = Decode(fixture.rpc.Handle(
        R"({"jsonrpc":"2.0","id":4,"method":"library.remove","params":{"installation_id":"org.example.game","confirmed":true}})"));
    REQUIRE(removed.Root().Member("result"));
    CHECK(removed.Root().Member("result")->Member("demo_hidden")->Bool() == true);
    CHECK_FALSE(shown());
}

TEST_CASE("GUI RPC exposes missing metadata as null facts") {
    Fixture fixture;
    std::ofstream(fixture.root / "library/org.example.game/meta.toml") << "broken";
    const auto response = Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"library.list"})"));
    const auto item = response.Root().Member("result")->Member("items")->Element(0);
    CHECK(item->Member("status")->String() == "damaged");
    CHECK_FALSE(item->Member("damage_reason")->String()->empty());
    CHECK(item->Member("version_name")->IsNull());
    CHECK(item->Member("version_code")->IsNull());
    CHECK(item->Member("imported_at")->IsNull());
    CHECK(item->Member("can_launch")->Bool() == false);
}

namespace {
std::string ImportCall(Fixture& fixture, std::string_view method, std::string_view params) {
    return fixture.rpc.Handle("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"" + std::string(method) +
                              "\",\"params\":" + std::string(params) + "}");
}

std::string PollImport(Fixture& fixture) {
    for (unsigned attempt = 0; attempt < 100000; ++attempt) {
        auto response = ImportCall(fixture, "library.job.poll", R"({"job":"1"})");
        const auto document = Decode(response);
        REQUIRE_FALSE(document.Root().Member("error"));
        const auto state = document.Root().Member("result")->Member("state")->String();
        if (state != "analyzing" && state != "importing") return response;
        std::this_thread::yield();
    }
    FAIL("import job did not settle");
    return {};
}

void UploadFixture(Fixture& fixture) {
    auto started = Decode(ImportCall(fixture, "library.upload.begin", R"({"name":"游戏.APK","size":7})"));
    REQUIRE(started.Root().Member("result"));
    REQUIRE(Decode(ImportCall(fixture, "library.upload.chunk", R"({"job":"1","offset":0,"data":"Zml4dHVyZQ=="})"))
                .Root()
                .Member("result"));
    REQUIRE(Decode(ImportCall(fixture, "library.upload.finish", R"({"job":"1"})")).Root().Member("result"));
}
}  // namespace

TEST_CASE("GUI import snapshot creates a distinct instance and rejects repeated commit") {
    Fixture fixture;
    UploadFixture(fixture);
    const auto ready = Decode(PollImport(fixture));
    const auto summary = ready.Root().Member("result")->Member("summary");
    REQUIRE(summary);
    CHECK(summary->Member("existing_instances")->UnsignedInteger() == 1);
    CHECK(summary->Member("version_code")->UnsignedInteger() == 0);
    CHECK_FALSE(summary->Member("requires_external").has_value());
    CHECK_FALSE(
        Decode(ImportCall(fixture, "library.import", R"({"job":"1","new_instance":false})")).Root().Member("result"));
    REQUIRE(
        Decode(ImportCall(fixture, "library.import", R"({"job":"1","new_instance":true})")).Root().Member("result"));
    const auto completed = Decode(PollImport(fixture));
    CHECK(completed.Root().Member("result")->Member("installation_id")->String() == "org.example.game-2");
    CHECK_FALSE(
        Decode(ImportCall(fixture, "library.import", R"({"job":"1","new_instance":true})")).Root().Member("result"));
    CHECK(fixture.store.LoadEntries().size() == 2);
    CHECK_FALSE(std::filesystem::exists(fixture.root / ".gui-import-1"));
    CHECK(std::filesystem::file_size(fixture.root / "library/org.example.game-2/game.apk") == 7);
}

TEST_CASE("GUI import rejects malformed upload and cancelled jobs without publishing") {
    Fixture fixture;
    for (const auto params : {R"({"name":"x.xapk","size":1})", R"({"name":"x.apk","size":0})",
                              R"({"name":"x.apk","size":1073741825})", R"({"name":"x.apk","size":7,"extra":true})"})
        CHECK(Decode(ImportCall(fixture, "library.upload.begin", params)).Root().Member("error"));
    REQUIRE(
        Decode(ImportCall(fixture, "library.upload.begin", R"({"name":"x.apk","size":7})")).Root().Member("result"));
    CHECK(Decode(ImportCall(fixture, "library.upload.chunk", R"({"job":"1","offset":1,"data":"YQ=="})"))
              .Root()
              .Member("error"));
    CHECK(Decode(ImportCall(fixture, "library.upload.chunk", R"({"job":"1","offset":0,"data":"?"})"))
              .Root()
              .Member("error"));
    CHECK(Decode(ImportCall(fixture, "library.upload.finish", R"({"job":"1"})")).Root().Member("error"));
    CHECK(Decode(ImportCall(fixture, "library.job.poll", R"({"job":"unknown"})")).Root().Member("error"));
    REQUIRE(Decode(ImportCall(fixture, "library.job.cancel", R"({"job":"1"})")).Root().Member("result"));
    CHECK_FALSE(std::filesystem::exists(fixture.root / ".gui-import-1"));
    CHECK(Decode(ImportCall(fixture, "library.import", R"({"job":"1","new_instance":true})")).Root().Member("error"));
    CHECK(fixture.store.LoadEntries().size() == 1);
}

TEST_CASE("GUI import invalid external directory preserves ready analysis for retry") {
    Fixture fixture;
    UploadFixture(fixture);
    PollImport(fixture);
    CHECK(Decode(ImportCall(fixture, "library.import",
                            R"({"job":"1","new_instance":true,"external_dir":"missing-directory-fixture"})"))
              .Root()
              .Member("error"));
    CHECK(Decode(PollImport(fixture)).Root().Member("result")->Member("state")->String() == "ready");
    REQUIRE(Decode(ImportCall(fixture, "library.job.cancel", R"({"job":"1"})")).Root().Member("result"));
}

TEST_CASE("GUI Dashboard RPC only opens tracked ready instances") {
    Fixture fixture;
    const auto open = [&] {
        return Decode(fixture.rpc.Handle(
            R"({"jsonrpc":"2.0","id":1,"method":"dashboard.open","params":{"installation_id":"game"}})"));
    };
    CHECK(open().Root().Member("error").has_value());
    fixture.dashboards.push_back(
        {"game", 123, static_cast<std::uint16_t>(15971), "starting", "waiting", "disabled", "starting"});
    CHECK(open().Root().Member("error").has_value());
    fixture.dashboards[0].status = "ready";
    CHECK(open().Root().Member("result").has_value());
    CHECK(fixture.opened_dashboard == "game");
    auto list = Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"dashboard.list"})"));
    CHECK(list.Root().Member("result")->Member("items")->Size() == 1);
    CHECK(
        Decode(
            fixture.rpc.Handle(
                R"({"jsonrpc":"2.0","id":1,"method":"dashboard.open","params":{"installation_id":"game","url":"http://example.org"}})"))
            .Root()
            .Member("error")
            .has_value());
    fixture.dashboards.clear();
    CHECK(open().Root().Member("error").has_value());
    CHECK(fixture.reads == 0);
}

TEST_CASE("GUI RPC removes only the confirmed stopped installation") {
    Fixture fixture;
    const auto entry = fixture.store.EntriesRoot() / "org.example.game";
    const auto sibling = fixture.store.EntriesRoot() / "org.example.game-2";
    const auto sandbox = ogplay::app::LauncherSandboxRoot(fixture.root) / "org.example.game";
    const auto external = fixture.root / "external";
    std::filesystem::create_directories(sibling);
    std::filesystem::create_directories(sandbox);
    std::filesystem::create_directories(external);
    std::ofstream(sandbox / "save.dat") << "save";
    std::ofstream(external / "data.obb") << "data";
    std::ofstream(fixture.root / "original.apk") << "source";
    for (const auto* params :
         {R"({"installation_id":"org.example.game"})", R"({"installation_id":"org.example.game","confirmed":false})",
          R"({"installation_id":"org.example.game","confirmed":1})",
          R"({"installation_id":"org.example.game","confirmed":true,"path":"../sandbox"})",
          R"({"installation_id":"org.example.game","confirmed":true,"delete_saves":"yes"})",
          R"({"installation_id":"../sandbox","confirmed":true})", R"({"installation_id":"absent","confirmed":true})"}) {
        const auto response = Decode(fixture.rpc.Handle(
            std::string(R"({"jsonrpc":"2.0","id":1,"method":"library.remove","params":)") + params + "}"));
        CHECK(response.Root().Member("error").has_value());
        CHECK(std::filesystem::exists(entry / "game.apk"));
    }
    const auto request =
        R"({"jsonrpc":"2.0","id":1,"method":"library.remove","params":{"installation_id":"org.example.game","confirmed":true}})";
    fixture.context.running_packages = {"org.example.game"};
    std::ofstream(entry / "settings.toml") << "invalid TOML [";
    CHECK(Decode(fixture.rpc.Handle(request)).Root().Member("error").has_value());
    CHECK(std::filesystem::exists(entry / "game.apk"));
    fixture.context.running_packages.clear();
    {
        ogplay::app::RunningLease externally_running(fixture.root, "org.example.game");
        auto rejected = Decode(fixture.rpc.Handle(request));
        REQUIRE(rejected.Root().Member("error"));
        CHECK(rejected.Root().Member("error")->Member("code")->Integer() == -32002);
        CHECK(std::filesystem::exists(entry / "game.apk"));
    }
    const auto removed = Decode(fixture.rpc.Handle(request));
    REQUIRE(removed.Root().Member("result").has_value());
    CHECK(removed.Root().Member("result")->Member("removed")->Bool() == true);
    CHECK_FALSE(std::filesystem::exists(entry));
    CHECK(std::filesystem::is_directory(sibling));
    CHECK(std::filesystem::exists(sandbox / "save.dat"));
    CHECK(std::filesystem::exists(external / "data.obb"));
    CHECK(std::filesystem::exists(fixture.root / "original.apk"));
    CHECK(Decode(fixture.rpc.Handle(request)).Root().Member("error").has_value());

    const auto sibling_saves = ogplay::app::LauncherSandboxRoot(fixture.root) / "org.example.game-2";
    std::filesystem::create_directories(sibling_saves);
    std::ofstream(sibling_saves / "save.dat") << "save";
    const auto with_saves = Decode(fixture.rpc.Handle(
        R"({"jsonrpc":"2.0","id":1,"method":"library.remove","params":{"installation_id":"org.example.game-2","confirmed":true,"delete_saves":true}})"));
    REQUIRE(with_saves.Root().Member("result").has_value());
    CHECK_FALSE(std::filesystem::exists(sibling));
    CHECK_FALSE(std::filesystem::exists(sibling_saves));
    CHECK(std::filesystem::exists(sandbox / "save.dat"));
}

TEST_CASE("GUI RPC clears the saves of a confirmed stopped installation and keeps its identity") {
    Fixture fixture;
    const auto entry = fixture.store.EntriesRoot() / "org.example.game";
    // A sandbox as the runtime writes it: identity in meta.toml, guest writes 1:1 under the four roots.
    const auto sandbox = ogplay::app::LauncherSandboxRoot(fixture.root) / "org.example.game";
    const std::string meta =
        "schema = 3\npackage = \"org.example.game\"\nversion_code = 1\nandroid_id = \"0123456789abcdef\"\n";
    for (const auto* root : {"internal/files", "external", "obb", "sdcard/vendor"})
        std::filesystem::create_directories(sandbox / root);
    std::ofstream(sandbox / "meta.toml") << meta;
    std::ofstream(sandbox / "internal/files/save.dat") << "save";
    std::ofstream(sandbox / "sdcard/vendor/profile.sav") << "profile";
    const auto saved = [&] {
        std::size_t count{};
        for (const auto* root : {"internal", "external", "obb", "sdcard"})
            count += static_cast<std::size_t>(
                std::distance(std::filesystem::recursive_directory_iterator(sandbox / root), {}));
        return count;
    };
    REQUIRE(saved() == 4);
    for (const auto* params :
         {R"({"installation_id":"org.example.game"})", R"({"installation_id":"org.example.game","confirmed":false})",
          R"({"installation_id":"org.example.game","confirmed":true,"delete_saves":true})",
          R"({"installation_id":"../sandbox","confirmed":true})", R"({"installation_id":"absent","confirmed":true})"}) {
        const auto response = Decode(fixture.rpc.Handle(
            std::string(R"({"jsonrpc":"2.0","id":1,"method":"library.clear_data","params":)") + params + "}"));
        CHECK(response.Root().Member("error").has_value());
    }
    CHECK(saved() == 4);
    const auto request =
        R"({"jsonrpc":"2.0","id":1,"method":"library.clear_data","params":{"installation_id":"org.example.game","confirmed":true}})";
    fixture.context.running_packages = {"org.example.game"};
    CHECK(Decode(fixture.rpc.Handle(request)).Root().Member("error")->Member("code")->Integer() == -32002);
    fixture.context.running_packages.clear();
    {
        ogplay::app::RunningLease externally_running(fixture.root, "org.example.game");
        CHECK(Decode(fixture.rpc.Handle(request)).Root().Member("error").has_value());
    }
    CHECK(saved() == 4);
    const auto cleared = Decode(fixture.rpc.Handle(request));
    REQUIRE(cleared.Root().Member("result").has_value());
    CHECK(cleared.Root().Member("result")->Member("cleared")->Bool() == true);
    CHECK(saved() == 0);
    std::ifstream input(sandbox / "meta.toml");
    CHECK(std::string(std::istreambuf_iterator<char>(input), {}) == meta);
    CHECK(std::filesystem::exists(entry / "game.apk"));
    CHECK(std::filesystem::exists(entry / "meta.toml"));
}

TEST_CASE("INP-7 GUI opens the key editor only for a running instance whose editor is ready") {
    Fixture fixture;
    const auto open = [&] {
        return Decode(fixture.rpc.Handle(
            R"({"jsonrpc":"2.0","id":1,"method":"controls.open","params":{"installation_id":"game"}})"));
    };
    auto response = open();
    REQUIRE(response.Root().Member("error").has_value());
    CHECK(response.Root().Member("error")->Member("code")->Integer() == -32004);
    fixture.dashboards.push_back(
        {"game", 123, std::nullopt, "disabled", "MCP off", "starting", "等待按键设置服务启动。"});
    CHECK(open().Root().Member("error")->Member("code")->Integer() == -32002);
    CHECK(fixture.opened_editor.empty());
    fixture.dashboards[0].editor_status = "ready";
    CHECK(open().Root().Member("result")->Member("opened")->Bool() == true);
    CHECK(fixture.opened_editor == "game");
    const auto list = Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"dashboard.list"})"));
    const auto item = list.Root().Member("result")->Member("items")->Element(0);
    CHECK(item->Member("key_editor_status")->String() == "ready");
    CHECK_FALSE(item->Member("token").has_value());
    CHECK(
        Decode(
            fixture.rpc.Handle(
                R"({"jsonrpc":"2.0","id":1,"method":"controls.open","params":{"installation_id":"game","token":"x"}})"))
            .Root()
            .Member("error")
            .has_value());
}

TEST_CASE("gui_rpc system_language accompanies get and set and notifies only on actual change") {
    Fixture fixture;
    const auto get = [&] { return Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"settings.get"})")); };
    auto initial = get();
    const auto result = *initial.Root().Member("result");
    CHECK(result.Member("system_language")->String() == UiLanguageTag(SystemUiLanguage()));
    CHECK(result.Member("values")->Member("language")->String() == "system");
    for (const auto language : {"zh-CN", "en-US", "system", "system"}) {
        auto current = get();
        const auto config = LoadLibraryConfig(fixture.root);
        const auto previous = ResolveUiLanguage(std::get<std::string>(Setting(config, "language")));
        const auto count = fixture.language_changes.size();
        app::JsonWriter writer;
        const auto request = writer.Object(), params = writer.Object(), values = writer.Object();
        writer.AddString(request, "jsonrpc", "2.0");
        writer.AddInteger(request, "id", 2);
        writer.AddString(request, "method", "settings.set");
        writer.AddString(params, "revision", *current.Root().Member("result")->Member("revision")->String());
        writer.AddString(values, "language", language);
        writer.Add(params, "values", values);
        writer.Add(request, "params", params);
        auto saved = Decode(fixture.rpc.Handle(writer.Serialize(request)));
        const auto response = saved.Root().Member("result");
        REQUIRE(response);
        CHECK(response->Member("system_language")->String() == UiLanguageTag(SystemUiLanguage()));
        CHECK(response->Member("values")->Member("language")->String() == language);
        const auto next = ResolveUiLanguage(language);
        CHECK(fixture.language_changes.size() == count + (next != previous ? 1U : 0U));
        if (next != previous) CHECK(fixture.language_changes.back() == next);
    }
    CHECK(std::get<std::string>(Setting(LoadLibraryConfig(fixture.root), "language")) == "system");
}

TEST_CASE("gui_rpc error params keep the template separate from status values") {
    Fixture fixture;
    fixture.context.running_packages = {"org.example.game"};
    const auto document = Decode(fixture.rpc.Handle(
        R"({"jsonrpc":"2.0","id":3,"method":"library.launch","params":{"installation_id":"org.example.game"}})"));
    const auto error = document.Root().Member("error");
    REQUIRE(error);
    CHECK(error->Member("message")->String() == "游戏当前不可启动（{status}）");
    CHECK(error->Member("params")->Member("status")->String() == "running");
    CHECK(error->Member("next_step")->String() == "检查运行状态、Profile 和数据包目录。");
    CHECK(fixture.launches == 0);
    fixture.context.running_packages.clear();
    fixture.fail_template_launch = true;
    const auto exception = Decode(fixture.rpc.Handle(
        R"({"jsonrpc":"2.0","id":4,"method":"library.launch","params":{"installation_id":"org.example.game"}})"));
    const auto failure = exception.Root().Member("error");
    REQUIRE(failure);
    CHECK(failure->Member("message")->String() == "图标素材缺失：{path}");
    CHECK(failure->Member("params")->Member("path")->String() == "/missing/icon.png");
}
