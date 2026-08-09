#include <cstdint>

#include <doctest/doctest.h>

#include "ogplay/input/mouse_touch_mapper.h"

namespace {

constexpr ogplay::hal::WindowState kWindow{
    .open = true, .id = 7, .width = 1280, .height = 720};

ogplay::hal::InputEvent Pointer(const ogplay::hal::InputEventType type,
                                const float x, const float y,
                                const bool pressed = false,
                                const std::uint32_t device = 9) {
    return {.type = type,
            .window_id = kWindow.id,
            .device_id = device,
            .code = static_cast<std::int32_t>(
                ogplay::hal::PointerButton::primary),
            .x = x,
            .y = y,
            .pressed = pressed};
}

}  // namespace

TEST_CASE("mouse primary drag maps to one paired guest touch") {
    ogplay::input::MouseTouchMapper mapper;

    CHECK_FALSE(mapper.Map(
        Pointer(ogplay::hal::InputEventType::pointer_motion, 640, 360),
        kWindow, 640, 480));
    auto down = mapper.Map(
        Pointer(ogplay::hal::InputEventType::pointer_button, 640, 360, true),
        kWindow, 640, 480);
    REQUIRE(down.has_value());
    CHECK(mapper.Active());
    CHECK(down->code == 0);
    CHECK(down->x == doctest::Approx(320));
    CHECK(down->y == doctest::Approx(240));

    auto move = mapper.Map(
        Pointer(ogplay::hal::InputEventType::pointer_motion, 700, 390),
        kWindow, 640, 480);
    REQUIRE(move.has_value());
    CHECK(move->code == down->code);
    CHECK(move->pressed);
    CHECK(move->x == doctest::Approx(360));
    CHECK(move->y == doctest::Approx(260));

    auto up = mapper.Map(
        Pointer(ogplay::hal::InputEventType::pointer_button, 700, 390, false),
        kWindow, 640, 480);
    REQUIRE(up.has_value());
    CHECK(up->code == down->code);
    CHECK_FALSE(up->pressed);
    CHECK_FALSE(mapper.Active());
}

TEST_CASE("mouse touch mapper rejects hover black bars and unrelated input") {
    ogplay::input::MouseTouchMapper mapper;
    auto secondary = Pointer(
        ogplay::hal::InputEventType::pointer_button, 640, 360, true);
    secondary.code = static_cast<std::int32_t>(
        ogplay::hal::PointerButton::secondary);
    CHECK_FALSE(mapper.Map(secondary, kWindow, 640, 480));
    CHECK_FALSE(mapper.Map(
        Pointer(ogplay::hal::InputEventType::pointer_button, 100, 360, true),
        kWindow, 640, 480));
    CHECK_FALSE(mapper.Map(
        Pointer(ogplay::hal::InputEventType::pointer_motion, 640, 360),
        kWindow, 640, 480));

    const ogplay::hal::InputEvent key{
        .type = ogplay::hal::InputEventType::key, .code = 12, .pressed = true};
    const auto mapped_key = mapper.Map(key, kWindow, 640, 480);
    REQUIRE(mapped_key.has_value());
    CHECK(mapped_key->code == key.code);
}

TEST_CASE("mouse touch mapper preserves capture and closes outside content") {
    ogplay::input::MouseTouchMapper mapper;
    REQUIRE(mapper.Map(
        Pointer(ogplay::hal::InputEventType::pointer_button, 640, 360, true),
        kWindow, 640, 480));
    CHECK_FALSE(mapper.Map(
        Pointer(ogplay::hal::InputEventType::pointer_motion, 700, 390,
                false, 10),
        kWindow, 640, 480));
    CHECK_FALSE(mapper.Map(
        Pointer(ogplay::hal::InputEventType::pointer_motion, 100, 360),
        kWindow, 640, 480));
    CHECK(mapper.Active());

    const auto up = mapper.Map(
        Pointer(ogplay::hal::InputEventType::pointer_button, 1400, 800, false),
        kWindow, 640, 480);
    REQUIRE(up.has_value());
    CHECK(up->x >= 0);
    CHECK(up->x < 640);
    CHECK(up->y >= 0);
    CHECK(up->y < 480);
    CHECK_FALSE(mapper.Active());
}
