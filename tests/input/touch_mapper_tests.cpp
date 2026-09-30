#include <doctest/doctest.h>
#include "ogplay/input/touch_mapper.h"
#include "ogplay/session/android_input_mapping.h"

TEST_CASE("BND46 touch mapper preserves large host identities and multi pointer ordering") {
    using namespace ogplay;
    input::TouchMapper mapper;
    const hal::WindowState window{true, 7, 1280, 720};
    hal::InputEvent event;
    event.type = hal::InputEventType::touch_down;
    event.window_id = 7;
    event.touch_device_id = 0x100000001ULL;
    event.contact_id = 0x200000001ULL;
    event.x = 640; event.y = 360; event.pressure = 0.5F;
    event.key_modifiers = static_cast<std::uint32_t>(hal::KeyModifier::left_shift);
    auto first = mapper.Map(event, window, 640, 480);
    REQUIRE(first.has_value());
    CHECK(first->points[0].x == 320);
    CHECK(first->points[0].y == 240);
    CHECK(first->points[0].pressure == 0.5F);
    CHECK(session::MapAndroidTouch(*first).action == 0);
    CHECK(session::MapAndroidTouch(*first).meta_state == 0x41);
    event.contact_id = 1; event.x = 800;
    auto second = mapper.Map(event, window, 640, 480);
    REQUIRE(second.has_value());
    REQUIRE(second->points.size() == 2);
    CHECK(second->points[1].id != second->points[0].id);
    CHECK(session::MapAndroidTouch(*second).action == (5 | 1 << 8));
    event.contact_id = 0x200000001ULL; event.type = hal::InputEventType::touch_up;
    event.x = -100; event.y = 999;
    auto up = mapper.Map(event, window, 640, 480);
    REQUIRE(up.has_value());
    REQUIRE(up->points.size() == 2);
    CHECK(up->points[0].x == 0);
    CHECK(up->points[0].y < 480);
    CHECK(session::MapAndroidTouch(*up).action == 6);
    event.contact_id = 1; event.type = hal::InputEventType::touch_motion;
    event.x = 640; event.y = 360;
    auto move = mapper.Map(event, window, 640, 480);
    REQUIRE(move.has_value());
    REQUIRE(move->points.size() == 1);
    CHECK(move->points[0].id == second->points[1].id);
    auto cancel = mapper.Map({.type = hal::InputEventType::input_reset}, window, 640, 480);
    REQUIRE(cancel.has_value());
    CHECK(session::MapAndroidTouch(*cancel).action == 3);
    CHECK(session::MapAndroidTouch(*cancel).meta_state == 0x41);
    CHECK_FALSE(mapper.Active());
    CHECK_FALSE(mapper.Map(event, window, 640, 480).has_value());
    auto invalid = *move;
    invalid.changed_index = static_cast<std::size_t>(-1);
    CHECK_THROWS_AS(static_cast<void>(session::MapAndroidTouch(invalid)), std::invalid_argument);
    invalid.points.clear();
    invalid.changed_index = 0;
    CHECK_THROWS_AS(static_cast<void>(session::MapAndroidTouch(invalid)), std::invalid_argument);
}

TEST_CASE("BND46 touch mapper isolates devices mouse and black bar origins") {
    using namespace ogplay;
    input::TouchMapper mapper;
    const hal::WindowState window{true, 7, 1280, 720};
    hal::InputEvent touch{.type = hal::InputEventType::touch_down, .window_id = 7,
        .x = 20, .y = 20, .touch_device_id = 5, .contact_id = 9, .pressure = 1};
    CHECK_FALSE(mapper.Map(touch, window, 640, 480));
    touch.x = 500;
    REQUIRE(mapper.Map(touch, window, 640, 480).has_value());
    touch.touch_device_id = 6;
    CHECK_FALSE(mapper.Map(touch, window, 640, 480));
    hal::InputEvent mouse{.type = hal::InputEventType::pointer_button, .window_id = 7,
        .code = 0, .x = 500, .y = 30, .pressed = true};
    CHECK_FALSE(mapper.Map(mouse, window, 640, 480));
    REQUIRE(mapper.Map({.type = hal::InputEventType::app_background}, window, 640, 480));
    REQUIRE(mapper.Map(mouse, window, 640, 480));
    CHECK_FALSE(mapper.Map(touch, window, 640, 480));
    const auto cancel = mapper.Map({.type = hal::InputEventType::input_reset}, window, 640, 480);
    REQUIRE(cancel.has_value());
    REQUIRE(cancel->points.size() == 1);
    CHECK(cancel->phase == input::TouchPhase::cancel);
    CHECK_FALSE(mapper.Active());
}
