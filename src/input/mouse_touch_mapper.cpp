#include "ogplay/input/mouse_touch_mapper.h"

#include <algorithm>
#include <cmath>

namespace ogplay::input {
namespace {

[[nodiscard]] bool IsPointer(const hal::InputEventType type) noexcept {
    return type == hal::InputEventType::pointer_motion ||
           type == hal::InputEventType::pointer_button;
}

[[nodiscard]] bool IsPrimary(const hal::InputEvent& event) noexcept {
    return event.code ==
           static_cast<std::int32_t>(hal::PointerButton::primary);
}

[[nodiscard]] float GuestCoordinate(const float value,
                                    const std::uint32_t extent) noexcept {
    return std::min(value, std::nextafter(static_cast<float>(extent), 0.0F));
}

}  // namespace

std::optional<hal::InputEvent> MouseTouchMapper::Map(
    const hal::InputEvent& event, const hal::WindowState& window,
    const std::uint32_t guest_width, const std::uint32_t guest_height) {
    if (!IsPointer(event.type)) return event;
    if (event.type == hal::InputEventType::pointer_button && !IsPrimary(event)) {
        return std::nullopt;
    }

    const auto mapped = hal::MapDisplayPoint(
        event.x, event.y, guest_width, guest_height, window.width, window.height);
    if (event.type == hal::InputEventType::pointer_button && event.pressed) {
        if (active_ || !mapped.inside) return std::nullopt;
        active_ = true;
        window_id_ = event.window_id;
        device_id_ = event.device_id;
    } else {
        if (!active_ || event.window_id != window_id_ ||
            event.device_id != device_id_) {
            return std::nullopt;
        }
        if (event.type == hal::InputEventType::pointer_motion && !mapped.inside) {
            return std::nullopt;
        }
        if (event.type == hal::InputEventType::pointer_button) active_ = false;
    }

    auto result = event;
    result.code = 0;
    result.x = GuestCoordinate(mapped.x, guest_width);
    result.y = GuestCoordinate(mapped.y, guest_height);
    if (result.type == hal::InputEventType::pointer_motion) result.pressed = true;
    return result;
}

bool MouseTouchMapper::Active() const noexcept { return active_; }

void MouseTouchMapper::Reset() noexcept {
    active_ = false;
    window_id_ = 0;
    device_id_ = 0;
}

}  // namespace ogplay::input
