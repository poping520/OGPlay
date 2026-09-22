#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include "ogplay/core/encoding.h"
#include "ogplay/frontend/gui_rpc.h"
#include "ogplay/frontend/gui_settings.h"

namespace {
using namespace ogplay;
struct Fixture {
    static inline std::atomic<unsigned> sequence{};
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("ogplay-gui-rpc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++));
    frontend::LibraryStore store{root};
    frontend::LibraryViewContext context;
    unsigned launches{}, opens{}, reads{}, minimized{};
    bool fail_launch{};
    std::optional<frontend::LaunchPlan> plan;
    std::filesystem::path opened;
    frontend::GuiRpcService rpc{store, root / "ogplay.exe", {
        [this](const auto&) { ++reads; return context; },
        [this](const auto& value) { if (fail_launch) throw std::runtime_error("spawn failed"); ++launches; plan = value; },
        [this](const auto& path) { ++opens; opened = path; },
        [](const auto& path) {
            frontend::ApkImportAnalysis result;
            result.source_apk = path;
            result.display_name = "导入游戏";
            result.manifest.package = "org.example.game";
            result.manifest.version_code = 0;
            return result;
        },
        [](bool) { return std::async(std::launch::deferred, []() -> std::optional<std::filesystem::path> { return std::nullopt; }); },
        [] { return std::string("2026-09-22T00:00:00Z"); },
        [this] { ++minimized; }}};
    Fixture() {
        std::filesystem::create_directories(root);
        std::ofstream(root / "ogplay.exe") << "fixture";
        const auto entry = root / "library/org.example.game";
        std::filesystem::create_directories(entry);
        std::ofstream(entry / "game.apk") << "fixture";
        std::ofstream(entry / "meta.toml") <<
            "schema = 1\npackage = \"org.example.game\"\ndisplay_name = \"游戏 <script>\"\n"
            "version_code = 1\nversion_name = \"1.0\"\nimported_at = \"now\"\n";
        const auto profiles = root / "profiles";
        std::filesystem::create_directories(profiles);
        frontend::SaveGuiConfig(root, {.profiles_dir = profiles});
    }
    ~Fixture() { std::error_code error; std::filesystem::remove_all(root, error); }
};
core::JsonDocument Decode(const std::string& response) {
    core::JsonParseError error;
    auto result = core::JsonDocument::ParseStrict(response, error);
    REQUIRE(result.has_value());
    return std::move(*result);
}
}

TEST_CASE("GUI RPC rejects unknown schema before host operations") {
    Fixture fixture;
    for (const auto* request : {
        R"({"jsonrpc":"2.0","id":1,"method":"library.launch","params":{"installation_id":"org.example.game","argv":[]}})",
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
    CHECK(result->Member("fields")->Size() == frontend::GuiSettings().size());
    const auto revision = std::string(*result->Member("revision")->String());
    const auto update = [&](std::string_view token, std::string_view key, auto value) {
        core::JsonWriter writer;
        const auto request = writer.Object(), params = writer.Object(), values = writer.Object();
        writer.AddString(request, "jsonrpc", "2.0"); writer.AddUnsignedInteger(request, "id", 2);
        writer.AddString(request, "method", "settings.set");
        writer.AddString(params, "revision", token);
        if constexpr (std::is_same_v<decltype(value), unsigned>) writer.AddUnsignedInteger(values, key, value);
        else writer.AddString(values, key, value);
        writer.Add(params, "values", values); writer.Add(request, "params", params);
        return Decode(fixture.rpc.Handle(writer.Serialize(request)));
    };
    CHECK(update(revision, "theme", "light").Root().Member("result").has_value());
    auto saved = frontend::LoadGuiConfig(fixture.root);
    CHECK(saved.profiles_dir == fixture.root / "profiles");
    CHECK(std::get<std::string>(frontend::GuiSetting(saved, "theme")) == "light");
    CHECK(update(revision, "theme", "dark").Root().Member("error").has_value());
    auto current = get();
    const auto token = std::string(*current.Root().Member("result")->Member("revision")->String());
    CHECK(update(token, "supersample", 5u).Root().Member("error").has_value());
    CHECK(update(token, "supersample", "2").Root().Member("error").has_value());
    CHECK(update(token, "unknown", "value").Root().Member("error").has_value());
    CHECK(update(token, "profiles_dir", (fixture.root / "missing").generic_string()).Root().Member("error").has_value());
    CHECK(frontend::LoadGuiConfig(fixture.root) == saved);
    CHECK(Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":3,"method":"settings.open_dir","params":{"kind":"library"}})")).Root().Member("result").has_value());
    CHECK(fixture.opened == fixture.root);
    CHECK(Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":3,"method":"settings.open_dir","params":{"kind":"library","path":"C:/"}})")).Root().Member("error").has_value());
    CHECK(fixture.opens == 1);
    core::JsonWriter writer;
    const auto analyze = writer.Object(), params = writer.Object();
    writer.AddString(analyze, "jsonrpc", "2.0"); writer.AddUnsignedInteger(analyze, "id", 4);
    writer.AddString(analyze, "method", "library.analyze");
    writer.AddString(params, "path", (fixture.root / "library/org.example.game/game.apk").generic_string());
    writer.Add(analyze, "params", params);
    REQUIRE(Decode(fixture.rpc.Handle(writer.Serialize(analyze))).Root().Member("result"));
    CHECK(update(token, "theme", "dark").Root().Member("error").has_value());
    CHECK(frontend::LoadGuiConfig(fixture.root) == saved);
    std::ofstream(fixture.root / "config.toml", std::ios::binary) << "schema = 99\n";
    CHECK(get().Root().Member("error").has_value());
    CHECK(update(token, "theme", "dark").Root().Member("error").has_value());
    CHECK(std::filesystem::file_size(fixture.root / "config.toml") == 12);
}

TEST_CASE("GUI minimizes only after a successful launch when requested") {
    Fixture fixture;
    const auto* launch = R"({"jsonrpc":"2.0","id":1,"method":"library.launch","params":{"installation_id":"org.example.game"}})";
    REQUIRE(Decode(fixture.rpc.Handle(launch)).Root().Member("result"));
    CHECK(fixture.minimized == 0);
    auto config = frontend::LoadGuiConfig(fixture.root);
    frontend::SetGuiSetting(config, "minimize_on_launch", true);
    frontend::SaveGuiConfig(fixture.root, config);
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
        core::JsonWriter writer; const auto request = writer.Object(), params = writer.Object();
        writer.AddString(request, "jsonrpc", "2.0"); writer.AddUnsignedInteger(request, "id", 1);
        writer.AddString(request, "method", "game_settings.get"); writer.AddString(params, "installation_id", id);
        writer.Add(request, "params", params); return Decode(fixture.rpc.Handle(writer.Serialize(request)));
    };
    const auto set = [&](std::string_view revision, std::string_view values) {
        auto parsed = Decode(std::string(values)); core::JsonWriter writer;
        const auto request = writer.Object(), params = writer.Object();
        writer.AddString(request, "jsonrpc", "2.0"); writer.AddUnsignedInteger(request, "id", 2);
        writer.AddString(request, "method", "game_settings.set"); writer.AddString(params, "installation_id", "org.example.game");
        writer.AddString(params, "revision", revision); writer.Add(params, "values", writer.Copy(parsed.Root()));
        writer.Add(request, "params", params); return Decode(fixture.rpc.Handle(writer.Serialize(request)));
    };
    auto initial = get("org.example.game");
    REQUIRE(initial.Root().Member("result"));
    const auto revision = std::string(*initial.Root().Member("result")->Member("revision")->String());
    for (const auto values : {R"({"supersample":5})", R"({"unknown":true})", R"({"supersample":"2"})",
                              R"({"mcp_manual_step":true})", R"({"mute":null})", R"({"external_dir":"relative"})"})
        CHECK(set(revision, values).Root().Member("error").has_value());
    CHECK_FALSE(std::filesystem::exists(first / "settings.toml"));
    auto changed = set(revision, R"({"supersample":3,"mcp_enabled":true,"mcp_manual_step":true,"ephemeral_sandbox":true})");
    REQUIRE(changed.Root().Member("result"));
    CHECK(frontend::LoadGameSettings(second).values.empty());
    CHECK(set(revision, "{}").Root().Member("error").has_value());
    const auto preview = changed.Root().Member("result")->Member("previews")->Member("normal")->Member("argv");
    REQUIRE(Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":3,"method":"library.launch","params":{"installation_id":"org.example.game"}})")).Root().Member("result"));
    REQUIRE(fixture.plan); REQUIRE(preview->Size() == fixture.plan->argv.size());
    for (std::size_t i = 0; i < preview->Size(); ++i) CHECK(preview->Element(i)->String() == fixture.plan->argv[i]);
    const auto current = std::string(*changed.Root().Member("result")->Member("revision")->String());
    auto global = frontend::LoadGuiConfig(fixture.root);
    frontend::SetGuiSetting(global, "supersample", std::uint32_t{4}); frontend::SaveGuiConfig(fixture.root, global);
    CHECK(set(current, "{}").Root().Member("error").has_value());
    auto reload = get("org.example.game");
    const auto fresh = std::string(*reload.Root().Member("result")->Member("revision")->String());
    REQUIRE(set(fresh, "{}").Root().Member("result"));
    CHECK(frontend::LoadGameSettings(first).values.empty());
    CHECK(get("../org.example.game").Root().Member("error").has_value());
    std::ofstream(first / "settings.toml") << "schema = 99\n";
    CHECK(get("org.example.game").Root().Member("error").has_value());
    REQUIRE(get("org.example.game-2").Root().Member("result"));
    CHECK(set(fresh, "{}").Root().Member("error").has_value());
}

TEST_CASE("GUI library facts use instance external directory and isolate broken settings") {
    Fixture fixture;
    const auto directory = fixture.root / "library/org.example.game";
    std::ofstream(directory / "meta.toml", std::ios::app) << "profile_id = \"org.example.game\"\n";
    fixture.context.external_required_packages = {"org.example.game"};
    const auto list = [&] { return Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"library.list"})")); };
    auto before = list();
    CHECK(before.Root().Member("result")->Member("items")->Element(0)->Member("status")->String() == "missing_external");
    const auto external = fixture.root / "external"; std::filesystem::create_directories(external);
    frontend::SaveGameSettings(directory, {{{"external_dir", external.generic_string()}}});
    auto after = list();
    CHECK(after.Root().Member("result")->Member("items")->Element(0)->Member("status")->String() == "ready");
    REQUIRE(Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":2,"method":"library.open_dir","params":{"installation_id":"org.example.game","kind":"external"}})")).Root().Member("result"));
    CHECK(fixture.opened == external);
    std::filesystem::copy(directory, fixture.root / "library/org.example.game-2", std::filesystem::copy_options::recursive);
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
    CHECK(items->Element(0)->Member("status")->String() == "missing_profile");
    CHECK(items->Element(0)->Member("display_name")->String() == "游戏 <script>");
    CHECK(items->Element(0)->Member("can_launch")->Bool() == true);
    CHECK(items->Element(0)->Member("version_name")->String() == "1.0");
    CHECK(items->Element(0)->Member("version_code")->UnsignedInteger() == 1);
    CHECK(items->Element(0)->Member("imported_at")->String() == "now");
    CHECK(items->Element(0)->Member("sandbox_path")->String()->ends_with("/sandbox/org.example.game"));
    CHECK(items->Element(0)->Member("log_directory")->String()->ends_with("/library/org.example.game"));
    const auto* launch = R"({"jsonrpc":"2.0","id":2,"method":"library.launch","params":{"installation_id":"org.example.game"}})";
    auto started = Decode(fixture.rpc.Handle(launch));
    CHECK(started.Root().Member("result").has_value());
    REQUIRE(fixture.plan.has_value());
    CHECK(fixture.plan->argv.back() == "org.example.game");
    CHECK(fixture.plan->log_path == fixture.root / "library/org.example.game/last-run.log");
    fixture.context.running_packages = {"org.example.game"};
    CHECK(Decode(fixture.rpc.Handle(launch)).Root().Member("error").has_value());
    fixture.context.running_packages.clear();
    fixture.context.profile_catalog_error = "unavailable";
    CHECK(Decode(fixture.rpc.Handle(launch)).Root().Member("error").has_value());
    CHECK(fixture.launches == 1);
}

TEST_CASE("GUI RPC opens only an existing instance directory") {
    Fixture fixture;
    const auto* log = R"({"jsonrpc":"2.0","id":1,"method":"library.open_dir","params":{"installation_id":"org.example.game","kind":"log"}})";
    CHECK(Decode(fixture.rpc.Handle(log)).Root().Member("result").has_value());
    CHECK(fixture.opened == fixture.root / "library/org.example.game");
    const auto* sandbox = R"({"jsonrpc":"2.0","id":1,"method":"library.open_dir","params":{"installation_id":"org.example.game","kind":"sandbox"}})";
    CHECK(Decode(fixture.rpc.Handle(sandbox)).Root().Member("error").has_value());
    CHECK_FALSE(std::filesystem::exists(fixture.root / "sandbox/org.example.game"));
    const auto traversal = Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"library.open_dir","params":{"installation_id":"../../","kind":"log"}})"));
    CHECK(traversal.Root().Member("error")->Member("code")->Integer() == -32004);
    CHECK(fixture.opens == 1);
}

TEST_CASE("GUI RPC exposes missing metadata as null facts") {
    Fixture fixture;
    std::ofstream(fixture.root / "library/org.example.game/meta.toml") << "broken";
    const auto response = Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"library.list"})"));
    const auto item = response.Root().Member("result")->Member("items")->Element(0);
    CHECK(item->Member("status")->String() == "damaged");
    CHECK(item->Member("version_name")->IsNull());
    CHECK(item->Member("version_code")->IsNull());
    CHECK(item->Member("imported_at")->IsNull());
    CHECK(item->Member("can_launch")->Bool() == false);
}

namespace {
std::string ImportCall(Fixture& fixture, std::string_view method, std::string_view params) {
    return fixture.rpc.Handle("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"" + std::string(method) + "\",\"params\":" + std::string(params) + "}");
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
    REQUIRE(Decode(ImportCall(fixture, "library.upload.chunk", R"({"job":"1","offset":0,"data":"Zml4dHVyZQ=="})")).Root().Member("result"));
    REQUIRE(Decode(ImportCall(fixture, "library.upload.finish", R"({"job":"1"})")).Root().Member("result"));
}
}
TEST_CASE("GUI import snapshot creates a distinct instance and rejects repeated commit") {
    Fixture fixture;
    UploadFixture(fixture);
    const auto ready = Decode(PollImport(fixture));
    const auto summary = ready.Root().Member("result")->Member("summary");
    REQUIRE(summary);
    CHECK(summary->Member("existing_instances")->UnsignedInteger() == 1);
    CHECK(summary->Member("version_code")->UnsignedInteger() == 0);
    CHECK(summary->Member("requires_external")->IsNull());
    CHECK_FALSE(Decode(ImportCall(fixture, "library.import", R"({"job":"1","new_instance":false})")).Root().Member("result"));
    REQUIRE(Decode(ImportCall(fixture, "library.import", R"({"job":"1","new_instance":true})")).Root().Member("result"));
    const auto completed = Decode(PollImport(fixture));
    CHECK(completed.Root().Member("result")->Member("installation_id")->String() == "org.example.game-2");
    CHECK_FALSE(Decode(ImportCall(fixture, "library.import", R"({"job":"1","new_instance":true})")).Root().Member("result"));
    CHECK(fixture.store.LoadEntries().size() == 2);
    CHECK_FALSE(std::filesystem::exists(fixture.root / ".gui-import-1"));
    CHECK(std::filesystem::file_size(fixture.root / "library/org.example.game-2/game.apk") == 7);
}
TEST_CASE("GUI import rejects malformed upload and cancelled jobs without publishing") {
    Fixture fixture;
    for (const auto params : {R"({"name":"x.xapk","size":1})", R"({"name":"x.apk","size":0})", R"({"name":"x.apk","size":1073741825})", R"({"name":"x.apk","size":7,"extra":true})"})
        CHECK(Decode(ImportCall(fixture, "library.upload.begin", params)).Root().Member("error"));
    REQUIRE(Decode(ImportCall(fixture, "library.upload.begin", R"({"name":"x.apk","size":7})")).Root().Member("result"));
    CHECK(Decode(ImportCall(fixture, "library.upload.chunk", R"({"job":"1","offset":1,"data":"YQ=="})")).Root().Member("error"));
    CHECK(Decode(ImportCall(fixture, "library.upload.chunk", R"({"job":"1","offset":0,"data":"?"})")).Root().Member("error"));
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
    CHECK(Decode(ImportCall(fixture, "library.import", R"({"job":"1","new_instance":true,"external_dir":"missing-directory-fixture"})")).Root().Member("error"));
    CHECK(Decode(PollImport(fixture)).Root().Member("result")->Member("state")->String() == "ready");
    REQUIRE(Decode(ImportCall(fixture, "library.job.cancel", R"({"job":"1"})")).Root().Member("result"));
}
