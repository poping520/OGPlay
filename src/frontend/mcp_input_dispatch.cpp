#include "ogplay/frontend/mcp_input_dispatch.h"

#include "ogplay/agent/mcp_protocol.h"

namespace ogplay::frontend {

bool McpPointerDispatcher::Active() const noexcept { return active_; }

void McpPointerDispatcher::Cancel(agent::McpInputQueue* inputs) {
    if (inputs) inputs->CancelPending();
    active_ = false;
}

bool McpPointerDispatcher::Pump(agent::McpInputQueue* inputs, input::ControlMapper& mapper) {
    if (inputs == nullptr) return false;
    const auto event = inputs->TakeNextPointerEvent();
    if (!event.has_value()) return false;
    if (event->pressed) {
        mapper.SetExternalContact(kContactId, static_cast<float>(event->x),
                                  static_cast<float>(event->y));
    } else {
        mapper.ClearExternalContact(kContactId);
    }
    active_ = event->pressed;
    return true;
}

}  // namespace ogplay::frontend
