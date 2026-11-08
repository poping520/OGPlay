#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "ogplay/input/control_mapper.h"
#include "ogplay/session/android_input_mapping.h"

namespace {

using namespace ogplay;
using input::ControlOutput;
using input::TouchPhase;
using Kind = ControlOutput::Kind;

constexpr hal::WindowState kWindow{true, 7, 800, 480};

hal::InputEvent Key(const char* name, const bool pressed, const bool repeat = false) {
    hal::InputEvent event{.type = hal::InputEventType::key, .window_id = 7,
                          .code = input::ParseInputSource(std::string{"key:"} + name).code};
    event.pressed = pressed;
    event.repeat = repeat;
    return event;
}

hal::InputEvent Mouse(const hal::InputEventType type, const float x, const float y,
                      const bool pressed, const hal::PointerButton button = hal::PointerButton::primary) {
    return {.type = type, .window_id = 7, .code = static_cast<std::int32_t>(button),
            .x = x, .y = y, .pressed = pressed};
}

input::Binding Button(const char* source, const char* zone,
                      const input::PressMode press = input::PressMode::hold) {
    input::Binding binding;
    binding.sources = {input::ParseInputSource(source)};
    binding.zone = zone;
    binding.press = press;
    return binding;
}

input::Binding Stick(const char* zone) {
    input::Binding binding;
    binding.action = input::ActionKind::stick;
    for (const char* key : {"key:w", "key:a", "key:s", "key:d"})
        binding.sources.push_back(input::ParseInputSource(key));
    binding.zone = zone;
    binding.stick_radius = 0.5F;
    // Most cases check exact geometry; the glide itself has its own test.
    binding.stick_settle_frames = 0;
    binding.stick_ramp_frames = 1;
    return binding;
}

input::ControlConfig ActionConfig() {
    input::ControlConfig config = input::DefaultControlConfig();
    config.zones = {
        {.name = "move", .anchor = input::Anchor::bottom_left, .x = 20, .y = -20, .radius = 80},
        {.name = "attack", .anchor = input::Anchor::bottom_right, .x = -40, .y = -40, .radius = 50},
        {.name = "menu", .anchor = input::Anchor::top_left, .x = 10, .y = 10, .width = 60, .height = 40},
    };
    config.bindings.push_back(Stick("move"));
    config.bindings.push_back(Button("key:j", "attack"));
    return config;
}

std::vector<ControlOutput> Touches(const std::vector<ControlOutput>& outputs) {
    std::vector<ControlOutput> result;
    for (const auto& output : outputs) if (output.kind == Kind::touch) result.push_back(output);
    return result;
}

}  // namespace

TEST_CASE("INP-1 zones resolve from anchors and reject boxes outside the surface") {
    input::ControlMapper mapper(ActionConfig());
    mapper.SetSurface(800, 480);
    mapper.Accept(Key("j", true), kWindow);
    const auto down = mapper.Flush();
    REQUIRE(down.size() == 1);
    CHECK(down[0].touch.phase == TouchPhase::down);
    // bottom_right anchor, 100 px box, 40 px margins: center at (800 - 40 - 50, 480 - 40 - 50).
    CHECK(down[0].touch.points[0].x == 710);
    CHECK(down[0].touch.points[0].y == 390);

    input::ControlMapper narrow(ActionConfig());
    CHECK_THROWS_AS(narrow.SetSurface(150, 480), input::ControlConfigError);
    CHECK_THROWS_AS(narrow.SetSurface(0, 480), std::invalid_argument);

    // A surface size change cancels contacts held under the old layout.
    mapper.SetSurface(1024, 600);
    const auto cancel = mapper.Flush();
    REQUIRE(cancel.size() == 1);
    CHECK(cancel[0].touch.phase == TouchPhase::cancel);
    mapper.Accept(Key("j", false), kWindow);
    CHECK(mapper.Flush().empty());
}

TEST_CASE("INP-1 hold, tap and repeat buttons keep DOWN and UP at least one frame apart") {
    auto config = ActionConfig();
    config.bindings.push_back(Button("key:k", "menu", input::PressMode::tap));
    input::ControlMapper mapper(config);
    mapper.SetSurface(800, 480);

    // Pressed and released before the frame: still held for exactly one frame.
    mapper.Accept(Key("j", true), kWindow);
    mapper.Accept(Key("j", false), kWindow);
    auto frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.phase == TouchPhase::down);
    frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.phase == TouchPhase::up);
    CHECK(mapper.Flush().empty());

    // Tap releases on the next frame even while the key stays held.
    mapper.Accept(Key("k", true), kWindow);
    CHECK(mapper.Flush()[0].touch.phase == TouchPhase::down);
    CHECK(mapper.Flush()[0].touch.phase == TouchPhase::up);
    CHECK(mapper.Flush().empty());
    mapper.Accept(Key("k", false), kWindow);
    CHECK(mapper.Flush().empty());

    // Hold follows the key.
    mapper.Accept(Key("j", true), kWindow);
    CHECK(mapper.Flush().size() == 1);
    CHECK(mapper.Flush().empty());
    mapper.Accept(Key("j", true, true), kWindow);
    CHECK(mapper.Flush().empty());
    mapper.Accept(Key("j", false), kWindow);
    CHECK(mapper.Flush()[0].touch.phase == TouchPhase::up);

    // Repeat taps every repeat_frames while held.
    input::ControlConfig burst;
    burst.zones = config.zones;
    auto every = Button("key:l", "menu", input::PressMode::repeat);
    every.repeat_frames = 3;
    burst.bindings.push_back(every);
    input::ControlMapper repeater(burst);
    repeater.SetSurface(800, 480);
    repeater.Accept(Key("l", true), kWindow);
    std::vector<TouchPhase> phases;
    for (int i = 0; i < 7; ++i) {
        const auto out = repeater.Flush();
        phases.push_back(out.empty() ? TouchPhase::cancel : out[0].touch.phase);
    }
    CHECK(phases == std::vector{TouchPhase::down, TouchPhase::up, TouchPhase::cancel,
                                TouchPhase::down, TouchPhase::up, TouchPhase::cancel,
                                TouchPhase::down});
}

TEST_CASE("INP-1 stick presses at its origin then moves to normalized directions") {
    input::ControlMapper mapper(ActionConfig());
    mapper.SetSurface(800, 480);
    // move zone: bottom_left, 160 px box, 20 px margins -> center (100, 380); travel 80 * 0.5.
    mapper.Accept(Key("w", true), kWindow);
    auto frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.phase == TouchPhase::down);
    CHECK(frame[0].touch.points[0].x == 100);
    CHECK(frame[0].touch.points[0].y == 380);
    frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.phase == TouchPhase::move);
    CHECK(frame[0].touch.points[0].x == 100);
    CHECK(frame[0].touch.points[0].y == 340);

    mapper.Accept(Key("d", true), kWindow);
    frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    const float diagonal = 40.0F / std::sqrt(2.0F);
    CHECK(frame[0].touch.points[0].x == doctest::Approx(100 + diagonal));
    CHECK(frame[0].touch.points[0].y == doctest::Approx(380 - diagonal));

    // Opposite keys cancel the direction but keep the finger down at the origin.
    mapper.Accept(Key("w", false), kWindow);
    mapper.Accept(Key("a", true), kWindow);
    frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.points[0].x == 100);
    CHECK(frame[0].touch.points[0].y == 380);

    mapper.Accept(Key("a", false), kWindow);
    mapper.Accept(Key("d", false), kWindow);
    frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.phase == TouchPhase::up);
    CHECK_FALSE(mapper.Active());
}

TEST_CASE("INP-1 stick rests at its origin then glides to each direction like a finger") {
    auto config = ActionConfig();
    config.bindings[1].stick_settle_frames = 2;
    config.bindings[1].stick_ramp_frames = 4;
    input::ControlMapper mapper(config);
    mapper.SetSurface(800, 480);
    // Single key: DOWN at the origin, two resting frames, then 10 px per frame up to 40 px.
    mapper.Accept(Key("w", true), kWindow);
    auto frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.phase == TouchPhase::down);
    CHECK(frame[0].touch.points[0].y == 380);
    CHECK(mapper.Flush().empty());
    CHECK(mapper.Flush().empty());
    std::vector<float> ys;
    for (int i = 0; i < 5; ++i) {
        frame = mapper.Flush();
        if (!frame.empty()) ys.push_back(frame[0].touch.points[0].y);
    }
    CHECK(ys == std::vector<float>{370, 360, 350, 340});

    // A new direction glides from the current offset instead of jumping.
    mapper.Accept(Key("w", false), kWindow);
    mapper.Accept(Key("d", true), kWindow);
    frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.points[0].x > 100);
    CHECK(frame[0].touch.points[0].x < 140);
    for (int i = 0; i < 8; ++i) static_cast<void>(mapper.Flush());
    mapper.Accept(Key("d", false), kWindow);
    CHECK(mapper.Flush()[0].touch.phase == TouchPhase::up);

    // A fresh press starts again at the origin.
    mapper.Accept(Key("s", true), kWindow);
    frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.phase == TouchPhase::down);
    CHECK(frame[0].touch.points[0].x == 100);
    CHECK(frame[0].touch.points[0].y == 380);
}

TEST_CASE("INP-1 mouse, stick, button and external contacts share one multi-touch stream") {
    input::ControlMapper mapper(ActionConfig());
    mapper.SetSurface(800, 480);
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 400, 200, true), kWindow);
    mapper.Accept(Key("w", true), kWindow);
    mapper.Accept(Key("j", true), kWindow);
    mapper.SetExternalContact(42, 300, 100);
    auto frame = Touches(mapper.Flush());
    REQUIRE(frame.size() == 4);
    CHECK(session::MapAndroidTouch(frame[0].touch).action == 0);
    CHECK(session::MapAndroidTouch(frame[1].touch).action == (5 | 1 << 8));
    CHECK(session::MapAndroidTouch(frame[3].touch).action == (5 | 3 << 8));
    REQUIRE(frame[3].touch.points.size() == 4);
    std::vector<std::int32_t> ids;
    for (const auto& point : frame[3].touch.points) ids.push_back(point.id);
    CHECK(ids == std::vector<std::int32_t>{0, 1, 2, 3});

    // Mouse drag and stick travel coalesce into a single MOVE for the frame.
    mapper.Accept(Mouse(hal::InputEventType::pointer_motion, 410, 210, true), kWindow);
    mapper.Accept(Mouse(hal::InputEventType::pointer_motion, 420, 220, true), kWindow);
    frame = Touches(mapper.Flush());
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.phase == TouchPhase::move);
    CHECK(frame[0].touch.points[0].x == doctest::Approx(420));

    // Releasing the mouse leaves the other ids stable; a new contact reuses the free id.
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 420, 220, false), kWindow);
    frame = Touches(mapper.Flush());
    REQUIRE(frame.size() == 1);
    CHECK(session::MapAndroidTouch(frame[0].touch).action == (6 | 0 << 8));
    mapper.ClearExternalContact(42);
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 100, 100, true), kWindow);
    frame = Touches(mapper.Flush());
    REQUIRE(frame.size() == 2);
    CHECK(frame[0].touch.phase == TouchPhase::up);
    CHECK(frame[1].touch.phase == TouchPhase::down);
    CHECK(frame[1].touch.points.back().id == 0);
}

TEST_CASE("INP-1 bound keys are consumed while unbound keys pass through") {
    input::ControlMapper mapper(ActionConfig());
    mapper.SetSurface(800, 480);
    mapper.Accept(Key("q", true), kWindow);
    mapper.Accept(Key("q", true, true), kWindow);
    mapper.Accept(Key("j", true), kWindow);
    auto frame = mapper.Flush();
    REQUIRE(frame.size() == 3);
    CHECK(frame[0].kind == Kind::passthrough_key);
    CHECK(frame[1].kind == Kind::passthrough_key);
    CHECK(frame[1].passthrough.repeat);
    CHECK(frame[2].kind == Kind::touch);
    const auto android = session::MapControlOutput(frame[0]);
    REQUIRE(android.has_value());
    CHECK(android->code == 45);  // KEYCODE_Q

    // Escape is the built-in Back key and is never passed through as KEYCODE_ESCAPE.
    mapper.Accept(Key("escape", true), kWindow);
    mapper.Accept(Key("escape", false), kWindow);
    frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].kind == Kind::android_key);
    CHECK(frame[0].android_key.pressed);
    const auto back = session::MapControlOutput(frame[0]);
    REQUIRE(back.has_value());
    CHECK(back->type == runtime::AndroidBoundaryInputType::key);
    CHECK(back->code == 4);
    frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK_FALSE(frame[0].android_key.pressed);

    mapper.Accept(Key("q", false), kWindow);
    frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].kind == Kind::passthrough_key);
    CHECK_FALSE(frame[0].passthrough.pressed);
}

TEST_CASE("INP-1 modes switch bindings and release contacts of deactivated bindings") {
    auto config = ActionConfig();
    input::Binding toggle;
    toggle.mode = std::string{input::kEveryMode};
    toggle.action = input::ActionKind::mode;
    toggle.sources = {input::ParseInputSource("key:tab")};
    toggle.target_mode = "menu";
    config.bindings.push_back(toggle);
    input::Binding hold = toggle;
    hold.sources = {input::ParseInputSource("key:left_alt")};
    hold.target_mode = "aim";
    hold.mode_switch = input::ModeSwitch::hold;
    config.bindings.push_back(hold);
    auto menu_button = Button("key:j", "menu");
    menu_button.mode = "menu";
    config.bindings.push_back(menu_button);
    auto aim_button = Button("key:j", "menu", input::PressMode::tap);
    aim_button.mode = "aim";
    config.bindings.push_back(aim_button);
    input::ControlMapper mapper(config);
    mapper.SetSurface(800, 480);

    mapper.Accept(Key("j", true), kWindow);
    CHECK(mapper.Flush()[0].touch.points[0].x == 710);
    mapper.Accept(Key("tab", true), kWindow);
    CHECK(mapper.Mode() == "menu");
    auto frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.phase == TouchPhase::up);
    // The held key does not engage the menu binding until it is pressed again.
    CHECK(mapper.Flush().empty());
    mapper.Accept(Key("j", false), kWindow);
    mapper.Accept(Key("j", true), kWindow);
    frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.points[0].x == 40);  // menu rect center
    mapper.Accept(Key("j", false), kWindow);
    CHECK(mapper.Flush()[0].touch.phase == TouchPhase::up);

    mapper.Accept(Key("tab", false), kWindow);
    mapper.Accept(Key("tab", true), kWindow);
    CHECK(mapper.Mode() == input::kDefaultMode);

    // A hold mode overlays the base mode only while its key is held.
    mapper.Accept(Key("left_alt", true), kWindow);
    mapper.Accept(Key("j", true), kWindow);
    CHECK(mapper.Flush()[0].touch.points[0].x == 40);
    CHECK(mapper.Flush()[0].touch.phase == TouchPhase::up);
    mapper.Accept(Key("left_alt", false), kWindow);
    mapper.Accept(Key("j", false), kWindow);
    mapper.Accept(Key("j", true), kWindow);
    CHECK(mapper.Flush()[0].touch.points[0].x == 710);
}

TEST_CASE("INP-1 contact budget rejects overflow once per owner") {
    input::ControlMapper mapper;
    mapper.SetSurface(800, 480);
    for (std::uint64_t id = 0; id < input::ControlMapper::kMaxContacts + 1; ++id)
        mapper.SetExternalContact(id, static_cast<float>(id * 10), 50);
    CHECK(Touches(mapper.Flush()).size() == input::ControlMapper::kMaxContacts);
    CHECK(mapper.RejectedContacts() == 1);
    CHECK(mapper.Flush().empty());
    CHECK(mapper.RejectedContacts() == 1);
    mapper.ClearExternalContact(0);
    const auto frame = Touches(mapper.Flush());
    REQUIRE(frame.size() == 2);
    CHECK(frame[0].touch.phase == TouchPhase::up);
    CHECK(frame[1].touch.phase == TouchPhase::down);
    CHECK(mapper.RejectedContacts() == 1);
}

TEST_CASE("INP-1 host reset cancels every contact and requires fresh presses") {
    input::ControlMapper mapper(ActionConfig());
    mapper.SetSurface(800, 480);
    mapper.Accept(Key("w", true), kWindow);
    mapper.Accept(Key("q", true), kWindow);
    mapper.Accept({.type = hal::InputEventType::touch_down, .window_id = 7, .x = 500, .y = 100,
                   .touch_device_id = 3, .contact_id = 9, .pressure = 0.5F}, kWindow);
    CHECK(Touches(mapper.Flush()).size() == 2);
    mapper.Accept({.type = hal::InputEventType::app_background}, kWindow);
    auto frame = mapper.Flush();
    REQUIRE(frame.size() == 2);
    CHECK(frame[0].kind == Kind::touch);
    CHECK(frame[0].touch.phase == TouchPhase::cancel);
    CHECK(frame[0].touch.points.size() == 2);
    CHECK(frame[1].kind == Kind::passthrough_key);
    CHECK_FALSE(frame[1].passthrough.pressed);
    mapper.Accept(Key("w", false), kWindow);
    mapper.Accept(Key("q", false), kWindow);
    CHECK(mapper.Flush().empty());
    CHECK_FALSE(mapper.Active());

    mapper.Accept(Key("w", true), kWindow);
    CHECK(mapper.Flush().size() == 1);
    mapper.Reset();
    CHECK(mapper.Flush().empty());
    CHECK_THROWS_AS(mapper.Accept({.type = hal::InputEventType::touch_down, .window_id = 7,
                                   .x = 500, .y = 100, .pressure = 2}, kWindow),
                    std::invalid_argument);
}

TEST_CASE("INP-1 a bound mouse button replaces the direct touch for that button") {
    input::ControlConfig config;
    config.zones = {{.name = "fire", .anchor = input::Anchor::center, .radius = 20}};
    config.bindings.push_back(Button("mouse:left", "fire"));
    config.bindings.push_back(Button("mouse:right", "fire", input::PressMode::tap));
    config.bindings.back().mode = "alt";
    input::ControlMapper mapper(config);
    mapper.SetSurface(800, 480);
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 10, 10, true), kWindow);
    auto frame = mapper.Flush();
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].touch.points[0].x == 400);
    CHECK(frame[0].touch.points[0].y == 240);
    // Right button is unbound in the default mode and never becomes a touch.
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 10, 10, true, hal::PointerButton::secondary),
                  kWindow);
    CHECK(mapper.Flush().empty());
}

TEST_CASE("INP-1 invalid control configurations fail explicitly") {
    const auto expect_error = [](input::ControlConfig config) {
        CHECK_THROWS_AS(input::ControlMapper{std::move(config)}, input::ControlConfigError);
    };
    auto duplicate = ActionConfig();
    duplicate.bindings.push_back(Button("key:j", "menu"));
    expect_error(duplicate);
    auto every = ActionConfig();
    every.bindings.push_back(Button("key:escape", "menu"));
    expect_error(every);
    auto unknown_zone = ActionConfig();
    unknown_zone.bindings.push_back(Button("key:k", "nowhere"));
    expect_error(unknown_zone);
    auto repeat = ActionConfig();
    repeat.bindings.push_back(Button("key:k", "menu", input::PressMode::repeat));
    expect_error(repeat);
    auto stick = ActionConfig();
    stick.bindings[1].sources.pop_back();
    expect_error(stick);
    auto zone = ActionConfig();
    zone.zones.push_back({.name = "menu", .width = 1, .height = 1});
    expect_error(zone);
    auto key = input::DefaultControlConfig();
    key.bindings[0].android_key_code = 0;
    expect_error(key);

    CHECK_THROWS_AS(static_cast<void>(input::ParseInputSource("key:hyper")), input::ControlConfigError);
    CHECK_THROWS_AS(static_cast<void>(input::ParseInputSource("pad:a")), input::ControlConfigError);
    CHECK(input::ParseInputSource("key:f12").code == 69);
    CHECK(input::ParseInputSource("key:0").code == 39);
    CHECK(input::ParseAnchor("bottom_right") == input::Anchor::bottom_right);
    CHECK_THROWS_AS(static_cast<void>(input::ParseAnchor("middle")), input::ControlConfigError);
}

TEST_CASE("INP-5 reconfigure validates first then cancels the old layout") {
    input::ControlMapper mapper(ActionConfig());
    mapper.SetSurface(800, 480);
    mapper.Accept(Key("j", true), kWindow);
    REQUIRE(mapper.Flush().size() == 1);

    auto oversized = ActionConfig();
    oversized.zones[1].radius = 400;
    CHECK_THROWS_AS(static_cast<void>(mapper.Reconfigure(oversized)), input::ControlConfigError);
    CHECK(mapper.Layout().size() == 3);
    CHECK(mapper.Flush().empty());  // the old layout keeps running after a rejected change

    auto moved = ActionConfig();
    moved.zones[1].x = -100;
    const auto outputs = mapper.Reconfigure(moved);
    REQUIRE(outputs.size() == 1);
    CHECK(outputs[0].touch.phase == TouchPhase::cancel);
    const auto layout = mapper.Layout();
    REQUIRE(layout.size() == 3);
    CHECK(layout[1].name == "attack");
    CHECK(layout[1].center_x == 650);
    CHECK(layout[1].circle);
    CHECK_FALSE(layout[2].circle);
    CHECK(mapper.Config().zones[1].x == -100);
    mapper.Accept(Key("j", false), kWindow);
    mapper.Accept(Key("j", true), kWindow);
    CHECK(mapper.Flush()[0].touch.points[0].x == 650);
}

TEST_CASE("INP-5 input sources format back to their names") {
    for (const char* name : {"key:w", "key:0", "key:7", "key:f11", "key:space", "key:left_shift",
                             "mouse:left", "mouse:x2"}) {
        CHECK(input::FormatInputSource(input::ParseInputSource(name)) == name);
    }
    CHECK_THROWS_AS(static_cast<void>(input::FormatInputSource({input::SourceKind::key, 200})),
                    input::ControlConfigError);
}

TEST_CASE("INP-4 hints list zones of bindings active in the current modes") {
    auto config = ActionConfig();
    config.bindings.push_back(Button("key:k", "attack"));
    auto menu = Button("key:m", "menu");
    menu.mode = "map";
    config.bindings.push_back(menu);
    input::Binding hold_map;
    hold_map.action = input::ActionKind::mode;
    hold_map.mode = std::string(input::kEveryMode);
    hold_map.sources = {input::ParseInputSource("key:tab")};
    hold_map.target_mode = "map";
    hold_map.mode_switch = input::ModeSwitch::hold;
    config.bindings.push_back(hold_map);
    input::ControlMapper mapper(config);
    CHECK(mapper.Hints().empty());  // no surface yet
    mapper.SetSurface(800, 480);
    auto hints = mapper.Hints();
    REQUIRE(hints.size() == 2);
    CHECK(hints[0].zone.name == "move");
    CHECK(hints[0].stick);
    CHECK(hints[0].sources.size() == 4);
    CHECK(hints[1].zone.name == "attack");
    CHECK(hints[1].sources == std::vector{input::ParseInputSource("key:j"), input::ParseInputSource("key:k")});
    CHECK(hints[1].zone.center_x == 800 - 40 - 50);

    mapper.Accept(Key("tab", true), kWindow);
    static_cast<void>(mapper.Flush());
    hints = mapper.Hints();
    REQUIRE(hints.size() == 3);
    CHECK(hints[2].zone.name == "menu");
    CHECK_FALSE(hints[2].zone.circle);
}

namespace {

hal::InputEvent Wheel(const float x, const float y, const float delta_y) {
    return {.type = hal::InputEventType::pointer_wheel, .window_id = 7, .x = x, .y = y, .delta_y = delta_y};
}

float Span(const input::TouchSnapshot& snapshot) {
    REQUIRE(snapshot.points.size() == 2);
    CHECK(snapshot.points[0].y == snapshot.points[1].y);
    return std::abs(snapshot.points[1].x - snapshot.points[0].x);
}

// Flushes until the mapper goes idle, keeping each frame's touch outputs.
std::vector<std::vector<ControlOutput>> RunUntilIdle(input::ControlMapper& mapper) {
    std::vector<std::vector<ControlOutput>> frames;
    for (int i = 0; i < 200 && (frames.empty() || mapper.Active()); ++i)
        frames.push_back(Touches(mapper.Flush()));
    REQUIRE_FALSE(mapper.Active());
    return frames;
}

}  // namespace

TEST_CASE("INP-9 wheel up grips at the cursor, rests, spreads by one notch and lifts") {
    input::ControlMapper mapper;
    mapper.SetSurface(800, 480);
    mapper.Accept(Wheel(400, 300, 1), kWindow);
    const auto frames = RunUntilIdle(mapper);

    REQUIRE(frames[0].size() == 2);
    CHECK(frames[0][0].touch.phase == TouchPhase::down);
    CHECK(frames[0][0].touch.points.size() == 1);
    CHECK(frames[0][1].touch.phase == TouchPhase::down);
    // The primary (ACTION_DOWN) finger lands on the cursor; the second one 0.3 * 800 to its right.
    CHECK(frames[0][0].touch.points[0].x == 400);
    CHECK(frames[0][1].touch.points[1].x == doctest::Approx(640));
    CHECK(frames[0][1].touch.points[0].y == 300);
    CHECK(frames[1].empty());
    CHECK(frames[2].empty());

    float last = 240;
    std::size_t moves = 0;
    std::size_t up_frame = 0;
    for (std::size_t f = 3; f < frames.size(); ++f) {
        for (const auto& output : frames[f]) {
            if (output.touch.phase == TouchPhase::move) {
                CHECK(output.touch.points[0].x == 400);  // the primary never moves
                CHECK(Span(output.touch) > last);
                CHECK(Span(output.touch) - last <= doctest::Approx(16));  // 0.02 * 800 per frame
                last = Span(output.touch);
                ++moves;
            } else {
                CHECK(output.touch.phase == TouchPhase::up);
                up_frame = f;
            }
        }
    }
    CHECK(moves == 3);
    CHECK(last == doctest::Approx(240 * 1.2));
    REQUIRE(frames[up_frame].size() == 2);
    CHECK(frames[up_frame][1].touch.points.size() == 1);
    CHECK(frames[up_frame - 1].empty());  // still frames before lifting
}

TEST_CASE("INP-9 wheel down pinches in and a span limit regrips for the remainder") {
    input::ControlMapper mapper;
    mapper.SetSurface(800, 480);
    mapper.Accept(Wheel(400, 240, -1), kWindow);
    auto frames = RunUntilIdle(mapper);
    float last = 0;
    for (const auto& frame : frames)
        for (const auto& output : frame)
            if (output.touch.phase == TouchPhase::move) last = Span(output.touch);
    CHECK(last == doctest::Approx(240 / 1.2));

    // Twelve notches exceed one grip (240 -> 64 is about 7.2 notches); the rest regrips.
    mapper.Accept(Wheel(400, 240, -12), kWindow);
    frames = RunUntilIdle(mapper);
    std::vector<std::size_t> down_frames, up_frames;
    std::vector<float> spans;
    for (std::size_t f = 0; f < frames.size(); ++f) {
        for (const auto& output : frames[f]) {
            if (output.touch.phase == TouchPhase::down && output.touch.points.size() == 2) down_frames.push_back(f);
            if (output.touch.phase == TouchPhase::up && output.touch.points.size() == 1) up_frames.push_back(f);
            if (output.touch.phase == TouchPhase::move) spans.push_back(Span(output.touch));
        }
    }
    REQUIRE(down_frames.size() == 2);
    REQUIRE(up_frames.size() == 2);
    CHECK(down_frames[1] == up_frames[0] + 1);  // a lifted frame between the grips
    REQUIRE_FALSE(spans.empty());
    CHECK(std::ranges::min(spans) == doctest::Approx(64));  // 0.04 * 800 on each side
    // Pending input is capped at one full grip range (x7.5): 240 -> 64, then 240 -> 120.
    CHECK(spans.back() == doctest::Approx(120));
}

TEST_CASE("INP-9 wheel pinch keeps both fingers inside and never joins another contact") {
    input::ControlMapper mapper;
    mapper.SetSurface(800, 480);
    // The primary stays on the cursor even at an edge; the second finger takes the side with room.
    mapper.Accept(Wheel(10, 100, 1), kWindow);
    auto frames = RunUntilIdle(mapper);
    REQUIRE(frames[0].size() == 2);
    CHECK(frames[0][1].touch.points[0].x == 10);
    CHECK(frames[0][1].touch.points[1].x == doctest::Approx(250));
    mapper.Accept(Wheel(790, 100, 3), kWindow);
    frames = RunUntilIdle(mapper);
    REQUIRE(frames[0].size() == 2);
    CHECK(frames[0][1].touch.points[0].x == 790);
    CHECK(frames[0][1].touch.points[1].x == doctest::Approx(550));
    for (const auto& frame : frames)
        for (const auto& output : frame)
            for (const auto& point : output.touch.points)
                CHECK(point.x >= 0);

    // A held mouse drag swallows the wheel.
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 100, 100, true), kWindow);
    static_cast<void>(mapper.Flush());
    mapper.Accept(Wheel(400, 240, 1), kWindow);
    CHECK(mapper.Flush().empty());
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 100, 100, false), kWindow);
    frames = RunUntilIdle(mapper);
    for (const auto& frame : frames)
        for (const auto& output : frame)
            CHECK(output.touch.points.size() == 1);

    // Pressing the mouse during a pinch lifts both fingers before the mouse lands.
    mapper.Accept(Wheel(400, 240, 1), kWindow);
    REQUIRE(Touches(mapper.Flush()).size() == 2);
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 100, 100, true), kWindow);
    const auto lift = Touches(mapper.Flush());
    REQUIRE(lift.size() == 3);
    CHECK(lift[0].touch.phase == TouchPhase::up);
    CHECK(lift[1].touch.phase == TouchPhase::up);
    CHECK(lift[2].touch.phase == TouchPhase::down);
    CHECK(lift[2].touch.points.size() == 1);
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 100, 100, false), kWindow);
    static_cast<void>(RunUntilIdle(mapper));

    // Cancellation closes an active pinch with CANCEL; black bars and zero deltas do nothing.
    mapper.Accept(Wheel(400, 240, 1), kWindow);
    static_cast<void>(mapper.Flush());
    mapper.Cancel();
    const auto cancel = Touches(mapper.Flush());
    REQUIRE(cancel.size() == 1);
    CHECK(cancel[0].touch.phase == TouchPhase::cancel);
    CHECK_FALSE(mapper.Active());
    mapper.Accept(Wheel(400, 240, 0), kWindow);
    mapper.Accept(Wheel(400, 30, 1), {true, 7, 800, 600});  // top bar above the 800x480 content
    CHECK_FALSE(mapper.Active());
}

namespace {

hal::InputEvent Pinch(const hal::InputEventType type, const float scale = 1.0F, const float x = 400,
                      const float y = 240) {
    return {.type = type, .window_id = 7, .x = x, .y = y, .scale = scale};
}

std::vector<std::vector<ControlOutput>> FlushFrames(input::ControlMapper& mapper, const int count) {
    std::vector<std::vector<ControlOutput>> frames;
    for (int i = 0; i < count; ++i)
        frames.push_back(Touches(mapper.Flush()));
    return frames;
}

std::size_t CountPhase(const std::vector<std::vector<ControlOutput>>& frames, const TouchPhase phase,
                       const std::size_t points) {
    std::size_t count = 0;
    for (const auto& frame : frames)
        for (const auto& output : frame)
            if (output.touch.phase == phase && output.touch.points.size() == points) ++count;
    return count;
}

float LastSpan(const std::vector<std::vector<ControlOutput>>& frames) {
    float span = 0;
    for (const auto& frame : frames)
        for (const auto& output : frame)
            if (output.touch.phase == TouchPhase::move) span = Span(output.touch);
    return span;
}

}  // namespace

TEST_CASE("INP-9 trackpad pinch grips at the cursor, follows each scale and holds through pauses") {
    using Type = hal::InputEventType;
    input::ControlMapper mapper;
    mapper.SetSurface(800, 480);
    mapper.Accept(Pinch(Type::pinch_begin), kWindow);
    auto frames = FlushFrames(mapper, 1);
    REQUIRE(frames[0].size() == 2);
    CHECK(frames[0][1].touch.phase == TouchPhase::down);
    CHECK(frames[0][1].touch.points[0].x == 400);
    CHECK(frames[0][1].touch.points[1].x == doctest::Approx(640));

    for (int i = 0; i < 3; ++i)
        mapper.Accept(Pinch(Type::pinch_update, 1.1F), kWindow);
    frames = FlushFrames(mapper, 40);
    CHECK(LastSpan(frames) == doctest::Approx(240 * 1.1 * 1.1 * 1.1));
    CHECK(CountPhase(frames, TouchPhase::up, 1) == 0);  // fingers stay down while the gesture pauses
    CHECK(frames.back().empty());

    mapper.Accept(Pinch(Type::pinch_update, 0.5F), kWindow);
    frames = FlushFrames(mapper, 40);
    CHECK(LastSpan(frames) == doctest::Approx(240 * 1.1 * 1.1 * 1.1 * 0.5));
    CHECK(CountPhase(frames, TouchPhase::up, 1) == 0);

    mapper.Accept(Pinch(Type::pinch_end), kWindow);
    frames = RunUntilIdle(mapper);
    // The fingers were already still for the linger frames, so both lift together at once.
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].size() == 2);
    CHECK(frames[0][0].touch.phase == TouchPhase::up);
    CHECK(frames[0][1].touch.phase == TouchPhase::up);
}

TEST_CASE("INP-9 trackpad pinch past the span limit regrips and keeps holding") {
    using Type = hal::InputEventType;
    input::ControlMapper mapper;
    mapper.SetSurface(800, 480);
    mapper.Accept(Pinch(Type::pinch_begin), kWindow);
    static_cast<void>(mapper.Flush());
    mapper.Accept(Pinch(Type::pinch_update, 0.1F), kWindow);
    const auto frames = FlushFrames(mapper, 80);
    CHECK(CountPhase(frames, TouchPhase::down, 2) == 1);  // one regrip after the first grip
    CHECK(CountPhase(frames, TouchPhase::up, 1) == 1);
    CHECK(LastSpan(frames) == doctest::Approx(120));  // capped at one range: 240 -> 64, 240 -> 120
    CHECK(mapper.Active());
    mapper.Accept(Pinch(Type::pinch_end), kWindow);
    CHECK(CountPhase(RunUntilIdle(mapper), TouchPhase::up, 1) == 1);
}

TEST_CASE("INP-9 trackpad pinch needs an accepted begin and yields to the mouse") {
    using Type = hal::InputEventType;
    input::ControlMapper mapper;
    mapper.SetSurface(800, 480);
    mapper.Accept(Pinch(Type::pinch_update, 2.0F), kWindow);
    CHECK_FALSE(mapper.Active());
    mapper.Accept(Pinch(Type::pinch_begin, 1.0F, 400, 30), {true, 7, 800, 600});  // black bar
    mapper.Accept(Pinch(Type::pinch_update, 2.0F), kWindow);
    CHECK_FALSE(mapper.Active());

    // A begin during a mouse drag is refused, and so are its updates afterwards.
    mapper.Accept(Mouse(Type::pointer_button, 100, 100, true), kWindow);
    static_cast<void>(mapper.Flush());
    mapper.Accept(Pinch(Type::pinch_begin), kWindow);
    mapper.Accept(Mouse(Type::pointer_button, 100, 100, false), kWindow);
    mapper.Accept(Pinch(Type::pinch_update, 2.0F), kWindow);
    mapper.Accept(Pinch(Type::pinch_end), kWindow);
    for (const auto& frame : RunUntilIdle(mapper))
        for (const auto& output : frame)
            CHECK(output.touch.points.size() == 1);

    // The wheel does not mix into a held trackpad gesture.
    mapper.Accept(Pinch(Type::pinch_begin), kWindow);
    static_cast<void>(mapper.Flush());
    mapper.Accept(Wheel(400, 240, 5), kWindow);
    CHECK(CountPhase(FlushFrames(mapper, 20), TouchPhase::move, 2) == 0);

    // A mouse press lifts the pinch; later updates of that gesture are ignored.
    mapper.Accept(Mouse(Type::pointer_button, 100, 100, true), kWindow);
    const auto lift = Touches(mapper.Flush());
    REQUIRE(lift.size() == 3);
    CHECK(lift[0].touch.phase == TouchPhase::up);
    CHECK(lift[2].touch.phase == TouchPhase::down);
    mapper.Accept(Mouse(Type::pointer_button, 100, 100, false), kWindow);
    mapper.Accept(Pinch(Type::pinch_update, 2.0F), kWindow);
    for (const auto& frame : RunUntilIdle(mapper))
        for (const auto& output : frame)
            CHECK(output.touch.points.size() == 1);
}

namespace {

hal::InputEvent Motion(const float dx, const float dy) {
    return {.type = hal::InputEventType::pointer_motion, .window_id = 7, .x = 1, .y = 1, .delta_x = dx, .delta_y = dy};
}

// Index of the look binding after the built-in Esc binding.
constexpr std::size_t kLook = 1;

// The crosshair "aim" sits at (600, 240); ` locks the pointer and M toggles a "menu" mode without
// the look. On 800x480 the finger keeps 14.4 px (3 % of 480) from the surface edges.
input::ControlConfig LookConfig() {
    input::ControlConfig config = input::DefaultControlConfig();
    config.zones = {
        {.name = "aim", .anchor = input::Anchor::center, .x = 200, .radius = 10},
        {.name = "weapon", .anchor = input::Anchor::top_right, .x = -10, .y = 10, .radius = 30},
    };
    input::Binding look;
    look.action = input::ActionKind::look;
    look.sources = {input::ParseInputSource("mouse:motion")};
    look.zone = "aim";
    config.bindings.push_back(look);
    input::Binding lock;
    lock.action = input::ActionKind::lock;
    lock.mode = std::string(input::kEveryMode);
    lock.sources = {input::ParseInputSource("key:grave")};
    config.bindings.push_back(lock);
    input::Binding menu;
    menu.action = input::ActionKind::mode;
    menu.mode = std::string(input::kEveryMode);
    menu.sources = {input::ParseInputSource("key:m")};
    menu.target_mode = "menu";
    config.bindings.push_back(menu);
    return config;
}

void PressKey(input::ControlMapper& mapper, const char* name) {
    mapper.Accept(Key(name, true), kWindow);
    mapper.Accept(Key(name, false), kWindow);
}

// Locks the pointer with ` and flushes that frame.
void Lock(input::ControlMapper& mapper) {
    PressKey(mapper, "grave");
    static_cast<void>(mapper.Flush());
    REQUIRE(mapper.WantsRelativePointer());
}

}  // namespace

TEST_CASE("INP-10 mouse motion, wheel and lock sources parse, format and validate") {
    for (const char* text : {"mouse:motion", "wheel:up", "wheel:down"})
        CHECK(input::FormatInputSource(input::ParseInputSource(text)) == text);
    CHECK_THROWS_AS(static_cast<void>(input::ParseInputSource("wheel:left")), input::ControlConfigError);

    auto config = LookConfig();
    config.bindings[kLook].sources = {input::ParseInputSource("key:l")};
    CHECK_THROWS_AS(input::ControlMapper{config}, input::ControlConfigError);
    config = LookConfig();
    config.bindings.push_back(Button("mouse:motion", "weapon"));
    CHECK_THROWS_AS(input::ControlMapper{config}, input::ControlConfigError);
    config = LookConfig();
    config.bindings[kLook].look_sensitivity_y = 0;
    CHECK_THROWS_AS(input::ControlMapper{config}, input::ControlConfigError);
    config = LookConfig();
    config.bindings[kLook].look_max_offset_x = -1;
    CHECK_THROWS_AS(input::ControlMapper{config}, input::ControlConfigError);
    config = LookConfig();
    config.bindings[kLook + 1].sources = {input::ParseInputSource("mouse:right")};  // lock from a mouse button
    CHECK_THROWS_AS(input::ControlMapper{config}, input::ControlConfigError);
    config = ActionConfig();
    config.bindings[1].sources[0] = input::ParseInputSource("wheel:up");  // the stick
    CHECK_THROWS_AS(input::ControlMapper{config}, input::ControlConfigError);
}

TEST_CASE("INP-10 the pointer starts free and the lock key or the host toggles it") {
    input::ControlMapper mapper(LookConfig());
    mapper.SetSurface(800, 480);
    CHECK(mapper.PointerReleased());
    CHECK_FALSE(mapper.WantsRelativePointer());
    mapper.Accept(Motion(30, 0), kWindow);  // a free cursor turns nothing
    CHECK(mapper.Flush().empty());

    Lock(mapper);
    PressKey(mapper, "grave");
    static_cast<void>(mapper.Flush());
    CHECK_FALSE(mapper.WantsRelativePointer());
    CHECK(mapper.TogglePointerLock());
    CHECK(mapper.WantsRelativePointer());

    // A key edit keeps the lock.
    static_cast<void>(mapper.Reconfigure(LookConfig()));
    CHECK(mapper.WantsRelativePointer());

    // Without a look binding there is nothing to lock.
    auto plain = LookConfig();
    plain.bindings.erase(plain.bindings.begin() + static_cast<std::ptrdiff_t>(kLook));
    input::ControlMapper free_mapper(plain);
    free_mapper.SetSurface(800, 480);
    CHECK_FALSE(free_mapper.TogglePointerLock());
    CHECK(free_mapper.PointerReleased());
}

TEST_CASE("INP-10 look presses at the crosshair, follows motion at once and holds while locked") {
    input::ControlMapper mapper(LookConfig());
    mapper.SetSurface(800, 480);
    Lock(mapper);
    CHECK(mapper.Flush().empty());  // no motion, no finger

    mapper.Accept(Motion(10, -5), kWindow);
    auto out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.phase == TouchPhase::down);
    CHECK(out[0].touch.points[0].x == 600);
    CHECK(out[0].touch.points[0].y == 240);
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.phase == TouchPhase::move);
    CHECK(out[0].touch.points[0].x == doctest::Approx(610));
    CHECK(out[0].touch.points[0].y == doctest::Approx(235));

    // Several motions in one frame merge into one MOVE.
    mapper.Accept(Motion(1, 1), kWindow);
    mapper.Accept(Motion(2, 2), kWindow);
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.points[0].x == doctest::Approx(613));
    CHECK(out[0].touch.points[0].y == doctest::Approx(238));

    // A still mouse keeps the finger down; unlocking lifts it.
    for (int i = 0; i < 100; ++i)
        CHECK(mapper.Flush().empty());
    CHECK(mapper.Active());
    CHECK(mapper.TogglePointerLock());
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.phase == TouchPhase::up);
    CHECK_FALSE(mapper.Active());
}

TEST_CASE("INP-10 look travels the whole surface at a finger's pace and regrips over two frames") {
    // On 800x480 a finger moves at most 168 px (35 % of 480) per frame and at most two such steps
    // of motion wait, so the view stops soon after the mouse.
    input::ControlMapper mapper(LookConfig());
    mapper.SetSurface(800, 480);
    Lock(mapper);
    mapper.Accept(Motion(300, 0), kWindow);
    CHECK(Touches(mapper.Flush())[0].touch.phase == TouchPhase::down);
    auto out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.points[0].x == doctest::Approx(768));
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.points[0].x == doctest::Approx(800 - 14.4));  // the edge margin
    // The old finger lifts, and only the next frame does a new one land at the crosshair: a
    // same-frame lift and press of one pointer id reads as a jump back in per-frame engines.
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.phase == TouchPhase::up);
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.phase == TouchPhase::down);
    CHECK(out[0].touch.points.size() == 1);
    CHECK(out[0].touch.points[0].x == 600);
    out = Touches(mapper.Flush());
    CHECK(out[0].touch.points[0].x == doctest::Approx(600 + 300 - (800 - 14.4 - 600)));

    // A fast sweep left crosses the left half in finger-sized steps with no regrip; motion beyond
    // the two-step backlog is dropped rather than replayed after the mouse stops.
    mapper.Accept(Motion(-500, 0), kWindow);
    float last = 714.4F;
    std::size_t moves = 0;
    for (const auto& frame : FlushFrames(mapper, 8)) {
        for (const auto& output : frame) {
            REQUIRE(output.touch.phase == TouchPhase::move);
            CHECK(last - output.touch.points[0].x <= doctest::Approx(168));
            last = output.touch.points[0].x;
            ++moves;
        }
    }
    CHECK(moves == 2);
    CHECK(last == doctest::Approx(714.4F - 336));
}

TEST_CASE("INP-10 look max offsets limit the travel around the crosshair") {
    auto config = LookConfig();
    config.bindings[kLook].look_max_offset_x = 100;
    config.bindings[kLook].look_max_offset_y = 50;
    input::ControlMapper mapper(config);
    mapper.SetSurface(800, 480);
    Lock(mapper);
    mapper.Accept(Motion(150, 80), kWindow);
    float max_x = 0, max_y = 0, last_x = 0, last_y = 0;
    std::size_t regrips = 0;
    for (const auto& frame : FlushFrames(mapper, 10)) {
        for (const auto& output : frame) {
            if (output.touch.phase == TouchPhase::up) ++regrips;
            if (output.touch.phase != TouchPhase::move) continue;
            last_x = output.touch.points[0].x;
            last_y = output.touch.points[0].y;
            max_x = std::max(max_x, last_x);
            max_y = std::max(max_y, last_y);
        }
    }
    CHECK(max_x == doctest::Approx(700));
    CHECK(max_y == doctest::Approx(290));
    CHECK(regrips == 1);
    CHECK(last_x == doctest::Approx(650));
    CHECK(last_y == doctest::Approx(270));
}

TEST_CASE("INP-10 handoff regrip presses the new finger before lifting the old one") {
    auto config = LookConfig();
    config.bindings[kLook].look_handoff = true;
    input::ControlMapper mapper(config);
    mapper.SetSurface(800, 480);
    Lock(mapper);
    mapper.Accept(Motion(300, 0), kWindow);
    static_cast<void>(mapper.Flush());  // down
    static_cast<void>(mapper.Flush());  // 768
    static_cast<void>(mapper.Flush());  // 785.6, at the limit
    auto out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.phase == TouchPhase::down);
    REQUIRE(out[0].touch.points.size() == 2);  // both fingers are down for a frame
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 2);
    CHECK(out[0].touch.phase == TouchPhase::move);
    CHECK(out[1].touch.phase == TouchPhase::up);
    CHECK(out[1].touch.points[out[1].touch.changed_index].x == doctest::Approx(800 - 14.4));
    const auto& moved = out[0].touch.points;
    CHECK(std::ranges::any_of(moved, [](const auto& point) { return std::abs(point.x - 714.4F) < 0.01F; }));
    CHECK(mapper.Flush().empty());
}

TEST_CASE("INP-10 a button or key can free a locked pointer") {
    auto config = LookConfig();
    auto map = Button("key:tab", "weapon", input::PressMode::tap);
    map.release_pointer = true;
    config.bindings.push_back(map);
    input::ControlMapper mapper(config);
    mapper.SetSurface(800, 480);
    Lock(mapper);
    mapper.Accept(Key("tab", true), kWindow);
    const auto out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.points[0].x == 800 - 10 - 30);  // the map button still taps
    CHECK(mapper.PointerReleased());
    mapper.Accept(Key("tab", false), kWindow);

    auto stick = ActionConfig();
    stick.bindings[1].release_pointer = true;
    CHECK_THROWS_AS(input::ControlMapper{stick}, input::ControlConfigError);
}

TEST_CASE("INP-10 look sensitivity, inversion and settle frames") {
    auto config = LookConfig();
    config.bindings[kLook].look_sensitivity_x = 2;
    config.bindings[kLook].look_sensitivity_y = 0.5F;
    config.bindings[kLook].look_invert_y = true;
    config.bindings[kLook].look_settle_frames = 1;
    input::ControlMapper mapper(config);
    mapper.SetSurface(800, 480);
    Lock(mapper);
    mapper.Accept(Motion(10, 10), kWindow);
    static_cast<void>(mapper.Flush());
    CHECK(mapper.Flush().empty());  // one settle frame
    const auto out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.points[0].x == doctest::Approx(620));
    CHECK(out[0].touch.points[0].y == doctest::Approx(235));
}

TEST_CASE("INP-10 a locked pointer touches only through bindings until a mode frees it") {
    input::ControlMapper mapper(LookConfig());
    mapper.SetSurface(800, 480);
    Lock(mapper);
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 100, 100, true), kWindow);
    CHECK(mapper.Flush().empty());
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 100, 100, false), kWindow);
    // Wheel turns neither pinch nor tap while the look owns the mouse.
    mapper.Accept(Wheel(400, 240, 1), kWindow);
    CHECK(mapper.Flush().empty());

    mapper.Accept(Motion(5, 0), kWindow);
    REQUIRE(Touches(mapper.Flush()).size() == 1);
    mapper.Accept(Key("m", true), kWindow);
    auto out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.phase == TouchPhase::up);  // the look finger lifts with its mode
    CHECK_FALSE(mapper.WantsRelativePointer());
    mapper.Accept(Key("m", false), kWindow);
    mapper.Accept(Motion(50, 0), kWindow);
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 100, 100, true), kWindow);
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.points[0].x == 100);  // direct mouse is back

    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 100, 100, false), kWindow);
    static_cast<void>(mapper.Flush());
    PressKey(mapper, "m");
    static_cast<void>(mapper.Flush());
    CHECK(mapper.WantsRelativePointer());  // still locked when the look's mode returns
}

TEST_CASE("INP-10 a bound wheel taps once per whole notch instead of pinching") {
    auto config = LookConfig();
    config.bindings.erase(config.bindings.begin() + static_cast<std::ptrdiff_t>(kLook));  // no look: the cursor is free
    config.bindings.push_back(Button("wheel:up", "weapon", input::PressMode::tap));
    input::ControlMapper mapper(config);
    mapper.SetSurface(800, 480);
    CHECK_FALSE(mapper.WantsRelativePointer());

    mapper.Accept(Wheel(400, 240, 1), kWindow);
    auto out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.phase == TouchPhase::down);
    CHECK(out[0].touch.points[0].x == 800 - 10 - 30);
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.phase == TouchPhase::up);

    // Fractional trackpad deltas add up to one notch; the unbound direction does nothing.
    for (int i = 0; i < 2; ++i)
        mapper.Accept(Wheel(400, 240, 0.4F), kWindow);
    CHECK(mapper.Flush().empty());
    mapper.Accept(Wheel(400, 240, 0.4F), kWindow);
    CHECK(Touches(mapper.Flush()).size() == 1);
    static_cast<void>(mapper.Flush());
    mapper.Accept(Wheel(400, 240, -3), kWindow);
    CHECK(RunUntilIdle(mapper).front().empty());
}

TEST_CASE("INP-10 a released pointer pauses look and restores the direct mouse") {
    input::ControlMapper mapper(LookConfig());
    mapper.SetSurface(800, 480);
    Lock(mapper);
    mapper.Accept(Motion(5, 0), kWindow);
    REQUIRE(Touches(mapper.Flush()).size() == 1);

    mapper.SetPointerReleased(true);
    CHECK(mapper.PointerReleased());
    CHECK_FALSE(mapper.WantsRelativePointer());
    auto out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.phase == TouchPhase::up);
    mapper.Accept(Motion(40, 0), kWindow);  // moves the cursor, not the view
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 100, 100, true), kWindow);
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.points.size() == 1);
    CHECK(out[0].touch.points[0].x == 100);
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 100, 100, false), kWindow);
    static_cast<void>(RunUntilIdle(mapper));

    Lock(mapper);
    mapper.Accept(Motion(5, 0), kWindow);
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.points[0].x == 600);  // a fresh grip at the crosshair
}

TEST_CASE("INP-10 a released pointer clicks menus directly instead of pressing mouse bindings") {
    auto config = LookConfig();
    config.bindings.push_back(Button("mouse:left", "weapon"));
    config.bindings.push_back(Button("wheel:up", "weapon", input::PressMode::tap));
    input::ControlMapper mapper(config);
    mapper.SetSurface(800, 480);
    CHECK(mapper.Hints().empty());  // free pointer: no mouse-driven zones offered
    Lock(mapper);
    CHECK(mapper.Hints().size() == 2);  // aim and weapon

    // Fire held across the release still lets go through its binding.
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 100, 100, true), kWindow);
    auto out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.points[0].x == 800 - 10 - 30);
    mapper.SetPointerReleased(true);
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 100, 100, false), kWindow);
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.phase == TouchPhase::up);
    CHECK(mapper.Hints().empty());

    // Released: the left button touches at the cursor and its release ends that touch.
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 120, 200, true), kWindow);
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.phase == TouchPhase::down);
    CHECK(out[0].touch.points[0].x == doctest::Approx(120));
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 120, 200, false), kWindow);
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.phase == TouchPhase::up);
    CHECK_FALSE(mapper.Active());

    // The bound wheel pinches like an unbound one while released.
    mapper.Accept(Wheel(400, 240, 1), kWindow);
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 2);
    CHECK(out[1].touch.points.size() == 2);
    static_cast<void>(RunUntilIdle(mapper));

    Lock(mapper);
    mapper.Accept(Mouse(hal::InputEventType::pointer_button, 120, 200, true), kWindow);
    out = Touches(mapper.Flush());
    REQUIRE(out.size() == 1);
    CHECK(out[0].touch.points[0].x == 800 - 10 - 30);  // fire again
}

TEST_CASE("INP-10 the built-in Escape goes Back and frees a locked pointer") {
    input::ControlMapper mapper(LookConfig());
    mapper.SetSurface(800, 480);
    CHECK(input::DefaultControlConfig().bindings[0].release_pointer);
    Lock(mapper);
    mapper.Accept(Key("escape", true), kWindow);
    const auto out = mapper.Flush();
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == Kind::android_key);
    CHECK(out[0].android_key.key_code == 4);
    CHECK(mapper.PointerReleased());
    mapper.Accept(Key("escape", false), kWindow);
    static_cast<void>(mapper.Flush());

    // Unticked, Escape only goes Back.
    auto config = LookConfig();
    config.bindings[0].release_pointer = false;
    input::ControlMapper plain(config);
    plain.SetSurface(800, 480);
    Lock(plain);
    plain.Accept(Key("escape", true), kWindow);
    static_cast<void>(plain.Flush());
    CHECK_FALSE(plain.PointerReleased());
}
