#include "ogplay/frontend/mcp_input_dispatch.h"

#include "ogplay/agent/mcp_protocol.h"

namespace ogplay::frontend {

bool McpPointerDispatcher::Active() const noexcept { return active_; }

bool McpPointerDispatcher::SuppressWindowEvent(
    const hal::InputEventType type) const noexcept {
    return active_ &&
           (type == hal::InputEventType::pointer_motion ||
            type == hal::InputEventType::pointer_button ||
            type == hal::InputEventType::touch_down ||
            type == hal::InputEventType::touch_motion ||
            type == hal::InputEventType::touch_up ||
            type == hal::InputEventType::touch_cancel);
}

void McpPointerDispatcher::Cancel(agent::McpInputQueue* inputs) {
    if (inputs) inputs->CancelPending();
    active_ = false;
}

std::optional<runtime::AndroidBoundaryInput> McpPointerDispatcher::TakeNext(
    agent::McpInputQueue* inputs,
    const bool host_gesture_active) {
    if (inputs == nullptr || (!active_ && host_gesture_active)) {
        return std::nullopt;
    }
    const auto event = inputs->TakeNextPointerEvent();
    if (!event.has_value()) return std::nullopt;
    active_ = event->pressed;
    return runtime::AndroidBoundaryInput{
        event->type == agent::McpPointerEvent::Type::motion
            ? runtime::AndroidBoundaryInputType::pointer_motion
            : runtime::AndroidBoundaryInputType::pointer_button,
        0,
        static_cast<float>(event->x), static_cast<float>(event->y),
        event->pressed};
}

}  // namespace ogplay::frontend
