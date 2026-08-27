#include <doctest/doctest.h>

#include <cstdint>

#include "ogplay/session/android_input_mapping.h"

namespace {

[[nodiscard]] std::uint32_t Modifier(const ogplay::hal::KeyModifier value) {
    return static_cast<std::uint32_t>(value);
}

}  // namespace

TEST_CASE("host keyboard facts map to Android key code unicode meta and repeat") {
    const ogplay::hal::InputEvent event{
        .type = ogplay::hal::InputEventType::key,
        .timestamp_ns = 12'345'678U,
        .device_id = 7,
        .code = 4,  // USB HID / SDL physical A
        .pressed = true,
        .repeat = true,
        .key_symbol = 'A',
        .key_modifiers = Modifier(ogplay::hal::KeyModifier::left_shift),
    };

    const auto mapped = ogplay::session::MapAndroidInput(event);
    REQUIRE(mapped.has_value());
    CHECK(mapped->type == ogplay::runtime::AndroidBoundaryInputType::key);
    CHECK(mapped->code == 29);  // Android KEYCODE_A
    CHECK(mapped->scan_code == 4);
    CHECK(mapped->device_id == 7);
    CHECK(mapped->unicode_char == 'A');
    CHECK(mapped->meta_state == 0x41);
    CHECK(mapped->repeat_count == 1);
    CHECK(mapped->event_time_ms == 12);
}

TEST_CASE("non printable and unknown physical keys fail closed") {
    const auto arrow = ogplay::session::MapAndroidInput({
        .type = ogplay::hal::InputEventType::key,
        .code = 82,
        .pressed = true,
    });
    REQUIRE(arrow.has_value());
    CHECK(arrow->code == 19);
    CHECK(arrow->unicode_char == 0);

    const auto unknown = ogplay::session::MapAndroidInput({
        .type = ogplay::hal::InputEventType::key,
        .code = 999,
        .pressed = true,
        .key_symbol = 'x',
    });
    REQUIRE(unknown.has_value());
    CHECK(unknown->code == 0);
}

TEST_CASE("pointer input retains normalized pointer semantics") {
    const auto mapped = ogplay::session::MapAndroidInput({
        .type = ogplay::hal::InputEventType::pointer_button,
        .code = 0,
        .x = 10.5F,
        .y = 20.5F,
        .pressed = true,
    });
    REQUIRE(mapped.has_value());
    CHECK(mapped->type ==
          ogplay::runtime::AndroidBoundaryInputType::pointer_button);
    CHECK(mapped->x == doctest::Approx(10.5F));
    CHECK(mapped->y == doctest::Approx(20.5F));
    CHECK(mapped->pressed);
}
