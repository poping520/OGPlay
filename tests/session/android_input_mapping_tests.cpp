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


TEST_CASE("BND46 input timeline uses guest Clock and preserves gesture down time") {
    using namespace ogplay;
    session::AndroidInputTimeline timeline;
    runtime::AndroidBoundaryInput touch{runtime::AndroidBoundaryInputType::pointer_button, 0, 10, 20, true};
    touch.event_time_ms = 123456; // unrelated SDL uptime
    const auto down = timeline.Stamp(touch, 1'000'000'000);
    REQUIRE(down.has_value());
    CHECK(down->event_time_ns == 1'000'000'000);
    CHECK(down->down_time_ns == 1'000'000'000);
    CHECK(down->source == 0x1002);
    auto multi = *down;
    multi.action = 5 | (1 << 8);
    auto second = multi.pointers[0]; second.id = 3; second.axes[0] = 30;
    multi.pointers.push_back(second);
    const auto joined = timeline.Stamp(multi, 2'000'000'000);
    REQUIRE(joined.has_value());
    CHECK(joined->down_time_ns == down->down_time_ns);
    multi = *joined; multi.action = 6; // primary pointer leaves
    REQUIRE(timeline.Stamp(multi, 3'000'000'000).has_value());
    const auto cancelled = timeline.Cancel(4'000'000'000);
    REQUIRE(cancelled.size() == 1);
    CHECK(cancelled[0].action == 3);
    REQUIRE(cancelled[0].pointers.size() == 1);
    CHECK(cancelled[0].pointers[0].id == 3);
    CHECK(cancelled[0].x == 30);
    CHECK(cancelled[0].down_time_ns == 1'000'000'000);
    CHECK(cancelled[0].event_time_ns == 4'000'000'000);
    CHECK(timeline.Cancel(5'000'000'000).empty());
    CHECK_THROWS(static_cast<void>(timeline.Stamp(touch, 4'000'000'000)));
    CHECK_THROWS(static_cast<void>(timeline.Cancel(4'000'000'000)));
    touch.type = runtime::AndroidBoundaryInputType::pointer_motion;
    CHECK_FALSE(timeline.Stamp(touch, 5'000'000'000).has_value());
}

TEST_CASE("BND46 repeated keys and cancellation retain original press time") {
    using namespace ogplay;
    session::AndroidInputTimeline timeline;
    runtime::AndroidBoundaryInput key{runtime::AndroidBoundaryInputType::key, 29, 0, 0, true};
    const auto down = timeline.Stamp(key, 1'000'000'000);
    REQUIRE(down.has_value());
    const auto repeat = timeline.Stamp(key, 2'000'000'000);
    REQUIRE(repeat.has_value());
    CHECK(repeat->repeat_count == 1);
    CHECK(repeat->down_time_ns == down->down_time_ns);
    const auto cancelled = timeline.Cancel(3'000'000'000);
    REQUIRE(cancelled.size() == 1);
    CHECK_FALSE(cancelled[0].pressed);
    CHECK(cancelled[0].action == 1);
    CHECK((cancelled[0].flags & 0x20) != 0);
    CHECK(cancelled[0].down_time_ns == down->down_time_ns);
}
