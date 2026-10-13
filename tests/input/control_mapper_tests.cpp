#include <doctest/doctest.h>

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
