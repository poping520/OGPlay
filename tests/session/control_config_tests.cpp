#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "ogplay/session/control_config.h"
#include "ogplay/session/title_profile.h"

namespace {

using namespace ogplay;

const std::filesystem::path kTemplates =
    std::filesystem::path(OGPLAY_SOURCE_DIR) / "data" / "input" / "templates";

constexpr std::string_view kProfileHead = R"(schema = 4

[identity]
package = "org.example.game"

)";

session::TitleProfile Profile(const std::string& input) {
    return session::LoadTitleProfileText(std::string(kProfileHead) + input, "org.example.game");
}

bool Bound(const input::ControlConfig& config, const char* source, const char* mode = "default") {
    const auto wanted = input::ParseInputSource(source);
    for (const auto& binding : config.bindings) {
        if (binding.mode != mode) continue;
        for (const auto& item : binding.sources) if (item == wanted) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("INP-2 every bundled template builds once its declared zones are placed") {
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(kTemplates)) {
        if (entry.path().extension() != ".toml") continue;
        ++count;
        const auto loaded = session::LoadControlTemplate(entry.path());
        CHECK(loaded.id == entry.path().stem().string());
        session::ControlLayer profile;
        profile.template_id = loaded.id;
        float x = 0;
        for (const auto& name : loaded.zones) {
            profile.zones.push_back({.name = name, .anchor = input::Anchor::top_left,
                                     .x = x, .y = 10, .radius = 20});
            x += 50;
        }
        auto config = session::BuildControlConfig(&profile, nullptr, kTemplates);
        input::ControlMapper mapper(std::move(config));
        mapper.SetSurface(800, 480);
    }
    CHECK(count > 0);
}

TEST_CASE("INP-2 profile input merges template zones bindings and unbinds") {
    const auto profile = Profile(R"(
[input]
template = "stick_action"

[[input.zone]]
name = "move"
anchor = "bottom_left"
x = 20
y = -20
r = 80

[[input.zone]]
name = "primary"
anchor = "bottom_right"
x = -40
y = -40
r = 50

[[input.zone]]
name = "skill_1"
anchor = "right"
x = -40
w = 80
h = 60

[[input.binding]]
source = "key:l"
action = "none"

[[input.binding]]
source = "key:p"
action = "none"

[[input.binding]]
source = "key:space"
action = "button"
zone = "primary"
press = "tap"
)");
    REQUIRE(profile.input.has_value());
    CHECK(profile.input->template_id == "stick_action");
    CHECK(profile.input->zones.size() == 3);
    const auto config = session::BuildControlConfig(&*profile.input, nullptr, kTemplates);
    CHECK(Bound(config, "key:w"));
    CHECK(Bound(config, "key:j"));
    CHECK(Bound(config, "key:space"));
    CHECK_FALSE(Bound(config, "key:l"));
    CHECK_FALSE(Bound(config, "key:p"));
    CHECK(Bound(config, "key:escape", "*"));
    input::ControlMapper mapper(config);
    mapper.SetSurface(1024, 600);
    mapper.Accept({.type = hal::InputEventType::key, .code = input::ParseInputSource("key:space").code,
                   .pressed = true}, {true, 1, 1024, 600});
    const auto frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.points[0].x == 1024 - 40 - 50);
}

TEST_CASE("INP-2 user overrides replace sources and the template choice") {
    auto profile = Profile(R"(
[input]
template = "stick_action"
zone = [
  { name = "move", anchor = "bottom_left", x = 20, y = -20, r = 80 },
  { name = "primary", anchor = "bottom_right", x = -40, y = -40, r = 50 },
  { name = "skill_1", anchor = "right", x = -40, r = 30 },
  { name = "skill_2", anchor = "right", x = -40, y = -100, r = 30 },
  { name = "pause", anchor = "top_left", x = 10, y = 10, w = 60, h = 40 },
]
)").input;
    REQUIRE(profile.has_value());
    const auto user = session::LoadControlOverrideText(R"(schema = 1

[[zone]]
name = "primary"
anchor = "bottom_right"
x = -10
y = -10
r = 50

[[binding]]
source = "mouse:right"
action = "button"
zone = "primary"

[[binding]]
source = "key:j"
action = "button"
zone = "skill_1"
press = "tap"

[[binding]]
source = "key:escape"
action = "key"
mode = "default"
key = "menu"

[[binding]]
source = "key:tab"
action = "mode"
mode = "*"
target = "menu"
)");
    const auto config = session::BuildControlConfig(&*profile, &user, kTemplates);
    CHECK(Bound(config, "mouse:right"));
    CHECK_FALSE(Bound(config, "key:escape", "*"));
    bool j_on_skill = false;
    for (const auto& binding : config.bindings) {
        if (binding.sources == std::vector{input::ParseInputSource("key:j")})
            j_on_skill = binding.zone == "skill_1";
    }
    CHECK(j_on_skill);
    CHECK(config.zones[1].x == -10);
    input::ControlMapper mapper(config);
    mapper.SetSurface(800, 480);
}

TEST_CASE("INP-2 invalid input configuration fails explicitly") {
    const auto profile_error = [](const std::string& input) {
        CHECK_THROWS_AS(static_cast<void>(Profile(input)), session::TitleProfileError);
    };
    profile_error("[input]\nprofile = \"generic_touch\"\n");
    profile_error("[input]\n[[input.binding]]\nsource = \"key:j\"\naction = \"jump\"\n");
    profile_error("[input]\n[[input.binding]]\nsource = \"key:j\"\naction = \"button\"\nzone = \"a\"\npress = \"double\"\n");
    profile_error("[input]\n[[input.binding]]\nsource = \"keys:w,a\"\naction = \"button\"\nzone = \"a\"\n");
    profile_error("[input]\n[[input.zone]]\nname = \"a\"\nanchor = \"center\"\nr = 5\nw = 3\nh = 3\n");
    profile_error("[input]\n[[input.zone]]\nname = \"a\"\nanchor = \"center\"\nr = 5\n"
                  "[[input.zone]]\nname = \"a\"\nanchor = \"center\"\nr = 5\n");
    profile_error("[input]\n[[input.binding]]\nsource = \"key:j\"\naction = \"key\"\nkey = \"power\"\n");
    profile_error("[input]\n[[input.binding]]\nsource = \"key:hyper\"\naction = \"none\"\n");

    session::ControlLayer missing_zone;
    missing_zone.template_id = "stick_action";
    CHECK_THROWS_WITH_AS(static_cast<void>(session::BuildControlConfig(&missing_zone, nullptr, kTemplates)),
                         doctest::Contains("needs zone"), input::ControlConfigError);
    session::ControlLayer unknown;
    unknown.template_id = "no_such_template";
    CHECK_THROWS_AS(static_cast<void>(session::BuildControlConfig(&unknown, nullptr, kTemplates)),
                    input::ControlConfigError);

    CHECK_THROWS_AS(static_cast<void>(session::LoadControlOverrideText("schema = 2\n")),
                    input::ControlConfigError);
    CHECK_THROWS_AS(static_cast<void>(session::LoadControlOverrideText("schema = 1\nzones = []\n")),
                    input::ControlConfigError);
    CHECK_THROWS_AS(static_cast<void>(session::LoadControlTemplateText(
                        "schema = 1\nid = \"other\"\nsummary = \"x\"\nzones = [\"a\"]\n", "stick")),
                    input::ControlConfigError);
    CHECK_THROWS_AS(static_cast<void>(session::LoadControlTemplateText(
                        "schema = 1\nid = \"t\"\nsummary = \"x\"\nzones = [\"a\"]\n"
                        "[[binding]]\nsource = \"key:j\"\naction = \"button\"\nzone = \"b\"\n", "t")),
                    input::ControlConfigError);
}

TEST_CASE("INP-5 user layers round-trip through TOML and JSON") {
    const auto layer = session::LoadControlOverrideText(R"(schema = 1
template = "stick_action"

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
    const auto templates = session::ListControlTemplates(kTemplates);
    REQUIRE_FALSE(templates.empty());
    CHECK(templates.front().id == "stick_action");
    CHECK(session::ListControlTemplates(directory / "missing").empty());
    std::filesystem::remove_all(directory);
}
