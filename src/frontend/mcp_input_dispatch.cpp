#include "ogplay/frontend/mcp_input_dispatch.h"

#include "ogplay/agent/mcp_protocol.h"

namespace ogplay::frontend {

bool McpPointerDispatcher::Active() const noexcept { return active_; }

bool McpPointerDispatcher::SuppressWindowEvent(
    const hal::InputEventType type) const noexcept {
    return active_ &&
           (type == hal::InputEventType::pointer_motion ||
            type == hal::InputEventType::pointer_button);
}

std::optional<runtime::AndroidBoundaryInput> McpPointerDispatcher::TakeNext(
    agent::McpInputQueue* inputs,
    const input::MouseTouchMapper& mouse_touch) {
    if (inputs == nullptr || (!active_ && mouse_touch.Active())) {
        return std::nullopt;
    }
    const auto event = inputs->TakeNextPointerEvent();
    if (!event.has_value()) return std::nullopt;
    active_ = event->pressed;
    return runtime::AndroidBoundaryInput{
        runtime::AndroidBoundaryInputType::pointer_button, 0,
        static_cast<float>(event->x), static_cast<float>(event->y),
        event->pressed};
}

}  // namespace ogplay::frontend
