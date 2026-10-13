#pragma once

#include <cstdint>

#include "ogplay/input/control_mapper.h"

namespace ogplay::agent {
class McpInputQueue;
}

namespace ogplay::frontend {

// Feeds queued MCP click/swipe phases into the shared ControlMapper as one external
// contact, so MCP gestures coexist with desktop and bound-key contacts.
class McpPointerDispatcher final {
public:
    static constexpr std::uint64_t kContactId = 0x4d4350U;

    [[nodiscard]] bool Active() const noexcept;
    // Applies at most one queued pointer phase; called once per guest step.
    bool Pump(agent::McpInputQueue* inputs, input::ControlMapper& mapper);
    void Cancel(agent::McpInputQueue* inputs);

private:
    bool active_{};
};

}  // namespace ogplay::frontend
