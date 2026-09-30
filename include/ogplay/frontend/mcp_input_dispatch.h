#pragma once

#include <optional>

#include "ogplay/hal/window_input.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"

namespace ogplay::agent {
class McpInputQueue;
}

namespace ogplay::frontend {

class McpPointerDispatcher final {
public:
    [[nodiscard]] bool Active() const noexcept;
    [[nodiscard]] bool SuppressWindowEvent(
        hal::InputEventType type) const noexcept;
    [[nodiscard]] std::optional<runtime::AndroidBoundaryInput> TakeNext(
        agent::McpInputQueue* inputs,
        bool host_gesture_active);
    void Cancel(agent::McpInputQueue* inputs);

private:
    bool active_{};
};

}  // namespace ogplay::frontend
