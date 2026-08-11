#pragma once

#include <optional>

#include "ogplay/hal/window_input.h"
#include "ogplay/input/mouse_touch_mapper.h"
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
        const input::MouseTouchMapper& mouse_touch);

private:
    bool active_{};
};

}  // namespace ogplay::frontend
