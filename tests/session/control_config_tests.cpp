#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include "ogplay/session/control_config.h"
using namespace ogplay;

TEST_CASE("user input merges only with built-in defaults and rejects templates") {
    const auto empty = session::BuildControlConfig({});
    CHECK(empty.zones.empty());
    CHECK(empty.bindings.size() == input::DefaultControlConfig().bindings.size());
    const auto user = session::LoadControlOverrideText(R"(schema = 1
[[zone]]
name = "primary"
anchor = "bottom_right"
x = -40
y = -40
r = 50
[[binding]]
source = "key:space"
action = "button"
zone = "primary"
press = "tap"
[[binding]]
source = "key:escape"
action = "none"
)");
    const auto config = session::BuildControlConfig(user);
    REQUIRE(config.bindings.size() == 1);
    CHECK(config.bindings.front().sources.front() == input::ParseInputSource("key:space"));
    input::ControlMapper mapper(config);
    mapper.SetSurface(1024, 600);
    mapper.Accept({.type = hal::InputEventType::key, .code = input::ParseInputSource("key:space").code,
                   .pressed = true}, {true, 1, 1024, 600});
    const auto frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.points[0].x == 1024 - 40 - 50);
    CHECK_THROWS_WITH_AS(static_cast<void>(session::LoadControlOverrideText(
        "schema = 1\ntemplate = \"stick_action\"\n")), doctest::Contains("template"), input::ControlConfigError);
    const auto unplaced = session::LoadControlOverrideText(
        "schema = 1\n[[binding]]\nsource = \"key:j\"\naction = \"button\"\nzone = \"missing\"\n");
    CHECK_THROWS_WITH_AS(static_cast<void>(session::BuildControlConfig(unplaced)),
                        doctest::Contains("unplaced zone"), input::ControlConfigError);
    core::JsonParseError error;
    const auto legacy = core::JsonDocument::ParseStrict(R"({"template":"stick_action"})", error);
    REQUIRE(legacy.has_value());
    CHECK_THROWS_AS(static_cast<void>(session::DecodeControlLayerJson(legacy->Root())), input::ControlConfigError);
}

TEST_CASE("INP-5 user layers round-trip through TOML and JSON") {
    const auto layer = session::LoadControlOverrideText(R"(schema = 1

[[zone]]
name = "move"
anchor = "bottom_left"
x = 20.25
y = -20
r = 80

[[zone]]
name = "menu"
anchor = "top_right"
x = -10
y = 10
w = 60
h = 40

[[binding]]
source = "keys:up,left,down,right"
action = "stick"
zone = "move"
radius = 0.9
settle_frames = 3
ramp_frames = 5

[[binding]]
source = "key:escape"
action = "key"
mode = "default"
key = "menu"

[[binding]]
source = "key:tab"
action = "mode"
mode = "*"
target = "map"
switch = "hold"

[[binding]]
source = "key:l"
action = "none"

[[binding]]
source = "mouse:right"
action = "button"
zone = "menu"
press = "repeat"
repeat_frames = 4
)");
    const auto text = session::FormatControlLayer(layer);
    const auto reparsed = session::LoadControlOverrideText(text);
    CHECK(session::FormatControlLayer(reparsed) == text);
    CHECK(text.find("x = 20.25") != std::string::npos);
    CHECK(text.find("source = \"keys:up,left,down,right\"") != std::string::npos);
    CHECK(text.find("key = \"menu\"") != std::string::npos);
    REQUIRE(reparsed.bindings.size() == 5);
    CHECK(reparsed.bindings[0].binding.stick_settle_frames == 3);
    CHECK(reparsed.bindings[3].unbind);

    core::JsonWriter writer;
    const auto json = writer.Serialize(session::WriteControlLayerJson(writer, layer));
    core::JsonParseError error;
    const auto document = core::JsonDocument::ParseStrict(json, error);
    REQUIRE(document.has_value());
    CHECK(session::FormatControlLayer(session::DecodeControlLayerJson(document->Root())) == text);
    const auto bad = core::JsonDocument::ParseStrict(R"({"zone":[{"name":"a","anchor":"center","r":5,"extra":1}]})", error);
    REQUIRE(bad.has_value());
    CHECK_THROWS_AS(static_cast<void>(session::DecodeControlLayerJson(bad->Root())), input::ControlConfigError);
}

TEST_CASE("INP-5 saving replaces the file and missing files are empty layers") {
    const auto directory = std::filesystem::temp_directory_path() / "ogplay-inp5-save";
    std::filesystem::remove_all(directory);
    const auto path = directory / "nested" / "input.toml";
    CHECK(session::LoadControlOverrideIfPresent(path).zones.empty());
    session::ControlLayer layer;
    layer.zones.push_back({.name = "fire", .anchor = input::Anchor::center, .radius = 20});
    session::SaveControlLayer(path, layer);
    session::SaveControlLayer(path, layer);
    CHECK(session::LoadControlOverrideIfPresent(path).zones.size() == 1);
    CHECK_FALSE(std::filesystem::exists(directory / "nested" / "input.toml.tmp"));
    std::filesystem::remove_all(directory);
}
