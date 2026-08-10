#include "ogplay/frontend/mcp_input_dispatch.h"

#include <doctest/doctest.h>

#include "ogplay/agent/mcp_protocol.h"

TEST_CASE("MCP pointer dispatcher emits one phase per guest loop step") {
    ogplay::agent::McpInputQueue inputs;
    ogplay::input::MouseTouchMapper mouse;
    ogplay::frontend::McpPointerDispatcher dispatcher;
    REQUIRE(inputs.TryEnqueueClick(7U, 123U, 45U).has_value());

    const auto down = dispatcher.TakeNext(&inputs, mouse);
    REQUIRE(down.has_value());
    CHECK(down->type ==
          ogplay::runtime::AndroidBoundaryInputType::pointer_button);
    CHECK(down->code == 0);
    CHECK(down->x == 123.0F);
    CHECK(down->y == 45.0F);
    CHECK(down->pressed);
    CHECK(dispatcher.Active());
    CHECK(dispatcher.SuppressWindowEvent(
        ogplay::hal::InputEventType::pointer_motion));
    CHECK(dispatcher.SuppressWindowEvent(
        ogplay::hal::InputEventType::pointer_button));
    CHECK_FALSE(dispatcher.SuppressWindowEvent(
        ogplay::hal::InputEventType::key));

    const auto up = dispatcher.TakeNext(&inputs, mouse);
    REQUIRE(up.has_value());
    CHECK_FALSE(up->pressed);
    CHECK_FALSE(dispatcher.Active());
    CHECK_FALSE(dispatcher.TakeNext(&inputs, mouse).has_value());
    CHECK_FALSE(dispatcher.TakeNext(nullptr, mouse).has_value());
}

TEST_CASE("MCP pointer dispatcher waits for an active desktop gesture") {
    ogplay::agent::McpInputQueue inputs;
    ogplay::input::MouseTouchMapper mouse;
    ogplay::frontend::McpPointerDispatcher dispatcher;
    REQUIRE(inputs.TryEnqueueClick(1U, 10U, 20U).has_value());

    const ogplay::hal::WindowState window{
        .open = true, .id = 3U, .width = 100U, .height = 100U};
    const ogplay::hal::InputEvent mouse_down{
        .type = ogplay::hal::InputEventType::pointer_button,
        .window_id = 3U,
        .device_id = 4U,
        .code = static_cast<std::int32_t>(
            ogplay::hal::PointerButton::primary),
        .x = 30.0F,
        .y = 40.0F,
        .pressed = true};
    REQUIRE(mouse.Map(mouse_down, window, 100U, 100U).has_value());
    CHECK_FALSE(dispatcher.TakeNext(&inputs, mouse).has_value());
    CHECK(inputs.PendingGestures() == 1U);

    auto mouse_up = mouse_down;
    mouse_up.pressed = false;
    REQUIRE(mouse.Map(mouse_up, window, 100U, 100U).has_value());
    REQUIRE(dispatcher.TakeNext(&inputs, mouse).has_value());
}

TEST_CASE("MCP pointer dispatcher preserves swipe down motion and up phases") {
    ogplay::agent::McpInputQueue inputs;
    ogplay::input::MouseTouchMapper mouse;
    ogplay::frontend::McpPointerDispatcher dispatcher;
    REQUIRE(inputs.TryEnqueueSwipe(3U, 10U, 20U, 30U, 40U, 2U).has_value());

    const auto down = dispatcher.TakeNext(&inputs, mouse);
    REQUIRE(down.has_value());
    CHECK(down->type ==
          ogplay::runtime::AndroidBoundaryInputType::pointer_button);
    CHECK(down->pressed);
    CHECK(dispatcher.Active());

    const auto first_motion = dispatcher.TakeNext(&inputs, mouse);
    REQUIRE(first_motion.has_value());
    CHECK(first_motion->type ==
          ogplay::runtime::AndroidBoundaryInputType::pointer_motion);
    CHECK(first_motion->x == 20.0F);
    CHECK(first_motion->y == 30.0F);
    CHECK(first_motion->pressed);
    CHECK(dispatcher.Active());

    const auto second_motion = dispatcher.TakeNext(&inputs, mouse);
    REQUIRE(second_motion.has_value());
    CHECK(second_motion->type ==
          ogplay::runtime::AndroidBoundaryInputType::pointer_motion);
    CHECK(second_motion->x == 30.0F);
    CHECK(second_motion->y == 40.0F);

    const auto up = dispatcher.TakeNext(&inputs, mouse);
    REQUIRE(up.has_value());
    CHECK(up->type ==
          ogplay::runtime::AndroidBoundaryInputType::pointer_button);
    CHECK_FALSE(up->pressed);
    CHECK_FALSE(dispatcher.Active());
}
