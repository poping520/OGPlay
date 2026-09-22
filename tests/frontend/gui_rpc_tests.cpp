#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include "ogplay/frontend/gui_rpc.h"

namespace {
using namespace ogplay;
struct Fixture {
    static inline std::atomic<unsigned> sequence{};
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("ogplay-gui-rpc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++));
    frontend::LibraryStore store{root};
    frontend::LibraryViewContext context;
    unsigned launches{}, opens{}, reads{};
    std::optional<frontend::LaunchPlan> plan;
    std::filesystem::path opened;
    frontend::GuiRpcService rpc{store, root / "ogplay.exe", {
        [this](const auto&) { ++reads; return context; },
        [this](const auto& value) { ++launches; plan = value; },
        [this](const auto& path) { ++opens; opened = path; }}};
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

TEST_CASE("GUI RPC serializes model facts and enforces launch eligibility") {
    Fixture fixture;
    auto list = Decode(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":"list","method":"library.list"})"));
    CHECK(list.Root().Member("id")->String() == "list");
    const auto items = list.Root().Member("result")->Member("items");
    REQUIRE(items->Size() == 1);
    CHECK(items->Element(0)->Member("status")->String() == "missing_profile");
    CHECK(items->Element(0)->Member("display_name")->String() == "游戏 <script>");
    CHECK(items->Element(0)->Member("can_launch")->Bool() == true);
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
