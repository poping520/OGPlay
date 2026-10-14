#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "ogplay/agent/control_editor.h"
#include "ogplay/agent/mcp_protocol.h"
#include "ogplay/frontend/control_requests.h"

namespace {

using namespace ogplay;

const std::filesystem::path kTemplates =
    std::filesystem::path(OGPLAY_SOURCE_DIR) / "data" / "input" / "templates";

struct Harness final {
    input::ControlMapper mapper;
    bool frozen{};
    session::ControlLayer user;
    std::optional<std::filesystem::path> path;
    agent::FrameSnapshotStore frames;
    int freezes{};
    std::size_t delivered{};

    agent::ControlResponse Call(const std::string& method, const std::string& params = {}) {
        std::optional<core::JsonDocument> document;
        core::JsonValue value;
        if (!params.empty()) {
            core::JsonParseError error;
            document = core::JsonDocument::ParseStrict(params, error);
            REQUIRE(document.has_value());
            value = document->Root();
        }
        return frontend::HandleControlRequest(method, value, {
            .mapper = mapper, .frozen = frozen, .user = user, .profile_input = nullptr,
            .templates_directory = kTemplates, .config_path = path, .frames = &frames,
            .surface_width = 800, .surface_height = 480,
            .freeze = [this] { ++freezes; },
            .deliver = [this](const std::vector<input::ControlOutput>& outputs) { delivered += outputs.size(); },
        });
    }
};

constexpr std::string_view kLayer = R"({"layer":{"template":"stick_action","zone":[
  {"name":"move","anchor":"bottom_left","x":20,"y":-20,"r":80},
  {"name":"primary","anchor":"bottom_right","x":-40.5,"y":-40,"r":50},
  {"name":"skill_1","anchor":"right","x":-40,"r":30},
  {"name":"skill_2","anchor":"right","x":-40,"y":-100,"r":30},
  {"name":"pause","anchor":"top_left","x":10,"y":10,"w":60,"h":40}],
 "binding":[{"source":"key:space","action":"button","zone":"primary","press":"tap"}]}})";

}  // namespace

TEST_CASE("INP-5 control.apply hot-applies, saves and round-trips through control.get") {
    const auto directory = std::filesystem::temp_directory_path() /
        ("ogplay-control-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Harness harness;
    harness.mapper.SetSurface(800, 480);
    harness.path = directory / "input.toml";

    auto response = harness.Call("control.freeze");
    CHECK(response.json == R"({"result":{"frozen":true}})");
    CHECK(harness.Call("control.freeze").ok);
    CHECK(harness.freezes == 1);

    response = harness.Call("control.apply", std::string(kLayer));
    REQUIRE(response.ok);
    CHECK(response.json.find(R"("saved":true)") != std::string::npos);
    CHECK(harness.mapper.Layout().size() == 5);
    const auto saved = session::LoadControlOverride(*harness.path);
    CHECK(saved.template_id == "stick_action");
    CHECK(saved.zones[1].x == doctest::Approx(-40.5));
    CHECK(saved.bindings.size() == 1);

    response = harness.Call("control.get");
    REQUIRE(response.ok);
    CHECK(response.json.find(R"("frozen":true)") != std::string::npos);
    CHECK(response.json.find(R"("persistent":true)") != std::string::npos);
    CHECK(response.json.find(R"("id":"stick_action")") != std::string::npos);
    CHECK(response.json.find(R"("source":"key:space")") != std::string::npos);
    CHECK(response.json.find(R"("name":"primary","center_x":709.5)") != std::string::npos);

    CHECK(harness.Call("control.resume").json == R"({"result":{"frozen":false}})");
    std::filesystem::remove_all(directory);
}

TEST_CASE("INP-5 rejected layouts leave the running layout unchanged") {
    Harness harness;
    harness.mapper.SetSurface(800, 480);
    REQUIRE(harness.Call("control.apply", std::string(kLayer)).ok);
    CHECK(harness.Call("control.apply", std::string(kLayer)).json.find(R"("saved":false)") != std::string::npos);

    const auto oversized = harness.Call("control.apply",
        R"({"layer":{"zone":[{"name":"a","anchor":"center","r":900}],"binding":[{"source":"key:j","action":"button","zone":"a"}]}})");
    CHECK_FALSE(oversized.ok);
    CHECK(oversized.json.find("-32010") != std::string::npos);
    const auto unplaced = harness.Call("control.apply", R"({"layer":{"template":"stick_action"}})");
    CHECK(unplaced.json.find("needs zone") != std::string::npos);
    CHECK(harness.Call("control.apply", R"({"layer":{},"extra":1})").json.find("-32602") != std::string::npos);
    CHECK(harness.mapper.Layout().size() == 5);
    CHECK(harness.user.template_id == "stick_action");
    CHECK(harness.Call("control.get", R"({"x":1})").json.find("unknown parameter") != std::string::npos);
    CHECK(harness.Call("control.nope").json.find("-32601") != std::string::npos);
}

TEST_CASE("INP-5 control.frame returns the latest presented frame as JPEG") {
    Harness harness;
    CHECK(harness.Call("control.frame").json.find("no presented frame") != std::string::npos);
    static_cast<void>(harness.frames.Publish({4, 2, 9, std::vector<std::uint8_t>(4 * 2 * 4, 128)}));
    const auto frame = harness.Call("control.frame");
    REQUIRE(frame.ok);
    CHECK(frame.json.find(R"("mime_type":"image/jpeg")") != std::string::npos);
    CHECK(frame.json.find(R"("sequence":9)") != std::string::npos);
}
