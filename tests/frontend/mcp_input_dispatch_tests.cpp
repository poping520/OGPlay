#include "ogplay/frontend/mcp_input_dispatch.h"

#include <doctest/doctest.h>

#include "ogplay/agent/mcp_protocol.h"
#include "ogplay/session/android_input_mapping.h"

namespace {

using ogplay::input::TouchPhase;

std::vector<ogplay::input::ControlOutput> Step(ogplay::frontend::McpPointerDispatcher& dispatcher,
                                               ogplay::agent::McpInputQueue& inputs,
                                               ogplay::input::ControlMapper& mapper) {
    dispatcher.Pump(&inputs, mapper);
    return mapper.Flush();
}

}  // namespace

TEST_CASE("MCP pointer dispatcher emits one phase per guest loop step") {
    ogplay::agent::McpInputQueue inputs;
    ogplay::input::ControlMapper mapper;
    mapper.SetSurface(640, 480);
    ogplay::frontend::McpPointerDispatcher dispatcher;
    REQUIRE(inputs.TryEnqueueClick(7U, 123U, 45U).has_value());

    const auto down = Step(dispatcher, inputs, mapper);
    REQUIRE(down.size() == 1);
    CHECK(down[0].touch.phase == TouchPhase::down);
    CHECK(down[0].touch.points[0].x == 123.0F);
    CHECK(down[0].touch.points[0].y == 45.0F);
    CHECK(dispatcher.Active());

    const auto up = Step(dispatcher, inputs, mapper);
    REQUIRE(up.size() == 1);
    CHECK(up[0].touch.phase == TouchPhase::up);
    CHECK_FALSE(dispatcher.Active());
    CHECK(Step(dispatcher, inputs, mapper).empty());
    CHECK_FALSE(dispatcher.Pump(nullptr, mapper));
}

TEST_CASE("MCP pointer gestures coexist with an active desktop gesture") {
    ogplay::agent::McpInputQueue inputs;
    ogplay::input::ControlMapper mapper;
    mapper.SetSurface(100, 100);
    ogplay::frontend::McpPointerDispatcher dispatcher;
    REQUIRE(inputs.TryEnqueueClick(1U, 10U, 20U).has_value());

    const ogplay::hal::WindowState window{.open = true, .id = 3U, .width = 100U, .height = 100U};
    mapper.Accept({.type = ogplay::hal::InputEventType::pointer_button, .window_id = 3U,
                   .device_id = 4U, .code = static_cast<std::int32_t>(ogplay::hal::PointerButton::primary),
                   .x = 30.0F, .y = 40.0F, .pressed = true},
                  window);
    const auto both = Step(dispatcher, inputs, mapper);
    REQUIRE(both.size() == 2);
    CHECK(ogplay::session::MapAndroidTouch(both[0].touch).action == 0);
    CHECK(ogplay::session::MapAndroidTouch(both[1].touch).action == (5 | 1 << 8));
    REQUIRE(both[1].touch.points.size() == 2);
    CHECK(both[1].touch.points[0].id != both[1].touch.points[1].id);
}

TEST_CASE("MCP pointer dispatcher preserves swipe down motion and up phases") {
    ogplay::agent::McpInputQueue inputs;
    ogplay::input::ControlMapper mapper;
    mapper.SetSurface(640, 480);
    ogplay::frontend::McpPointerDispatcher dispatcher;
    REQUIRE(inputs.TryEnqueueSwipe(3U, 10U, 20U, 30U, 40U, 2U).has_value());

    const auto down = Step(dispatcher, inputs, mapper);
    REQUIRE(down.size() == 1);
    CHECK(down[0].touch.phase == TouchPhase::down);
    CHECK(dispatcher.Active());

    const auto first_motion = Step(dispatcher, inputs, mapper);
    REQUIRE(first_motion.size() == 1);
    CHECK(first_motion[0].touch.phase == TouchPhase::move);
    CHECK(first_motion[0].touch.points[0].x == 20.0F);
    CHECK(first_motion[0].touch.points[0].y == 30.0F);

    const auto second_motion = Step(dispatcher, inputs, mapper);
    REQUIRE(second_motion.size() == 1);
    CHECK(second_motion[0].touch.points[0].x == 30.0F);
    CHECK(second_motion[0].touch.points[0].y == 40.0F);

    const auto up = Step(dispatcher, inputs, mapper);
    REQUIRE(up.size() == 1);
    CHECK(up[0].touch.phase == TouchPhase::up);
    CHECK_FALSE(dispatcher.Active());
}

TEST_CASE("BND46 MCP lifecycle cancellation clears queued gestures and releases host touch") {
    ogplay::agent::McpInputQueue queue;
    ogplay::input::ControlMapper mapper;
    mapper.SetSurface(640, 480);
    ogplay::frontend::McpPointerDispatcher dispatcher;
    REQUIRE(queue.TryEnqueueClick(1, 10, 20).has_value());
    REQUIRE(dispatcher.Pump(&queue, mapper));
    CHECK(dispatcher.Active());
    dispatcher.Cancel(&queue);
    mapper.Reset();
    CHECK_FALSE(dispatcher.Active());
    CHECK(queue.PendingGestures() == 0);
    CHECK_FALSE(dispatcher.Pump(&queue, mapper));
    CHECK(mapper.Flush().empty());
}
