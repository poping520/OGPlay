#include "ogplay/input/touch_mapper.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ogplay::input {

bool TouchMapper::Active() const noexcept { return mouse_.Active() || !contacts_.empty(); }
void TouchMapper::Reset() noexcept { mouse_.Reset(); contacts_.clear(); modifiers_ = 0; }

std::optional<TouchSnapshot> TouchMapper::Map(const hal::InputEvent& event,
    const hal::WindowState& window, std::uint32_t width, std::uint32_t height) {
    using Type = hal::InputEventType;
    if (event.type == Type::input_reset || event.type == Type::app_background) {
        if (!Active()) return std::nullopt;
        TouchSnapshot result{TouchPhase::cancel};
        result.modifiers = modifiers_;
        if (mouse_.Active()) result.points.push_back(mouse_point_);
        else for (const auto& contact : contacts_) result.points.push_back(contact.point);
        Reset();
        return result;
    }
    if (event.type == Type::pointer_button || event.type == Type::pointer_motion) {
        if (!contacts_.empty()) return std::nullopt;
        const auto mapped = mouse_.Map(event, window, width, height);
        if (!mapped) return std::nullopt;
        const auto phase = mapped->type == Type::pointer_motion ? TouchPhase::move :
            mapped->pressed ? TouchPhase::down : TouchPhase::up;
        mouse_point_ = {0, mapped->x, mapped->y, mapped->pressed ? 1.0F : 0.0F};
        modifiers_ = event.key_modifiers;
        return TouchSnapshot{phase, 0, {mouse_point_}, modifiers_};
    }
    if (event.type != Type::touch_down && event.type != Type::touch_motion &&
        event.type != Type::touch_up && event.type != Type::touch_cancel) return std::nullopt;
    if (mouse_.Active()) return std::nullopt;
    if (!contacts_.empty() && (device_ != event.touch_device_id || window_ != event.window_id))
        return std::nullopt;
    auto found = std::find_if(contacts_.begin(), contacts_.end(),
        [&](const auto& contact) { return contact.host_id == event.contact_id; });
    if (event.type == Type::touch_cancel) {
        if (found == contacts_.end()) return std::nullopt;
        TouchSnapshot result{TouchPhase::cancel};
        result.modifiers = modifiers_;
        for (const auto& contact : contacts_) result.points.push_back(contact.point);
        Reset();
        return result;
    }
    const auto mapped = hal::MapDisplayPoint(event.x, event.y, width, height, window.width, window.height);
    if (!std::isfinite(event.pressure) || event.pressure < 0 || event.pressure > 1)
        throw std::invalid_argument("invalid touch pressure");
    if (event.type == Type::touch_down) {
        if (found != contacts_.end() || !mapped.inside) return std::nullopt;
        if (contacts_.size() >= 16) throw std::runtime_error("touch contact budget exhausted");
        std::int32_t id = 0;
        while (std::ranges::any_of(contacts_, [id](const auto& c) { return c.point.id == id; })) ++id;
        contacts_.push_back({event.contact_id, {id}});
        found = contacts_.end() - 1;
        device_ = event.touch_device_id; window_ = event.window_id;
    } else if (found == contacts_.end()) return std::nullopt;
    found->point.x = std::min(mapped.x, std::nextafter(static_cast<float>(width), 0.0F));
    found->point.y = std::min(mapped.y, std::nextafter(static_cast<float>(height), 0.0F));
    found->point.pressure = event.pressure;
    TouchSnapshot result;
    modifiers_ = event.key_modifiers;
    result.modifiers = modifiers_;
    result.phase = event.type == Type::touch_down ? TouchPhase::down :
        event.type == Type::touch_up ? TouchPhase::up : TouchPhase::move;
    result.changed_index = static_cast<std::size_t>(found - contacts_.begin());
    for (const auto& contact : contacts_) result.points.push_back(contact.point);
    if (event.type == Type::touch_up) contacts_.erase(found);
    return result;
}
} // namespace ogplay::input
