#pragma once

#include <cstdint>
#include <optional>

#include "ogplay/hal/window_input.h"

namespace ogplay::input {

class MouseTouchMapper final {
public:
    [[nodiscard]] std::optional<hal::InputEvent> Map(
        const hal::InputEvent& event, const hal::WindowState& window,
        std::uint32_t guest_width, std::uint32_t guest_height);

    [[nodiscard]] bool Active() const noexcept;
    void Reset() noexcept;

private:
    bool active_{};
    std::uint32_t window_id_{};
    std::uint32_t device_id_{};
};

}  // namespace ogplay::input
