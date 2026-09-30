#include <stdexcept>
#include <limits>
#include "ogplay/session/android_input_mapping.h"

#include <cstdint>

namespace ogplay::session {
namespace {

// SDL scancodes use the USB HID usage values for the common keyboard page.
// Keep that host physical identity at the HAL edge and translate it here.
[[nodiscard]] std::int32_t AndroidKeyCode(const std::int32_t scan_code) {
    if (scan_code >= 4 && scan_code <= 29) return 29 + scan_code - 4;
    if (scan_code >= 30 && scan_code <= 38) return 8 + scan_code - 30;
    if (scan_code == 39) return 7;
    if (scan_code >= 58 && scan_code <= 69) return 131 + scan_code - 58;
    if (scan_code >= 89 && scan_code <= 97) return 145 + scan_code - 89;

    switch (scan_code) {
    case 40: return 66;   // ENTER
    case 41: return 111;  // ESCAPE
    case 42: return 67;   // DEL / backspace
    case 43: return 61;   // TAB
    case 44: return 62;   // SPACE
    case 45: return 69;   // MINUS
    case 46: return 70;   // EQUALS
    case 47: return 71;   // LEFT_BRACKET
    case 48: return 72;   // RIGHT_BRACKET
    case 49: return 73;   // BACKSLASH
    case 51: return 74;   // SEMICOLON
    case 52: return 75;   // APOSTROPHE
    case 53: return 68;   // GRAVE
    case 54: return 55;   // COMMA
    case 55: return 56;   // PERIOD
    case 56: return 76;   // SLASH
    case 57: return 115;  // CAPS_LOCK
    case 70: return 120;  // PRINT_SCREEN / SYSRQ
    case 71: return 116;  // SCROLL_LOCK
    case 72: return 121;  // PAUSE / BREAK
    case 73: return 124;  // INSERT
    case 74: return 122;  // MOVE_HOME
    case 75: return 92;   // PAGE_UP
    case 76: return 112;  // FORWARD_DEL
    case 77: return 123;  // MOVE_END
    case 78: return 93;   // PAGE_DOWN
    case 79: return 22;   // DPAD_RIGHT
    case 80: return 21;   // DPAD_LEFT
    case 81: return 20;   // DPAD_DOWN
    case 82: return 19;   // DPAD_UP
    case 83: return 143;  // NUM_LOCK
    case 84: return 154;  // NUMPAD_DIVIDE
    case 85: return 155;  // NUMPAD_MULTIPLY
    case 86: return 156;  // NUMPAD_SUBTRACT
    case 87: return 157;  // NUMPAD_ADD
    case 88: return 160;  // NUMPAD_ENTER
    case 98: return 144;  // NUMPAD_0
    case 99: return 158;  // NUMPAD_DOT
    case 224: return 113; // CTRL_LEFT
    case 225: return 59;  // SHIFT_LEFT
    case 226: return 57;  // ALT_LEFT
    case 227: return 117; // META_LEFT
    case 228: return 114; // CTRL_RIGHT
    case 229: return 60;  // SHIFT_RIGHT
    case 230: return 58;  // ALT_RIGHT
    case 231: return 118; // META_RIGHT
    default: return 0;    // KEYCODE_UNKNOWN
    }
}

[[nodiscard]] bool Has(const std::uint32_t modifiers,
                       const hal::KeyModifier flag) {
    return (modifiers & static_cast<std::uint32_t>(flag)) != 0U;
}

[[nodiscard]] std::int32_t AndroidMetaState(const std::uint32_t modifiers) {
    std::int32_t state{};
    if (Has(modifiers, hal::KeyModifier::left_shift)) state |= 0x41;
    if (Has(modifiers, hal::KeyModifier::right_shift)) state |= 0x81;
    if (Has(modifiers, hal::KeyModifier::left_alt)) state |= 0x12;
    if (Has(modifiers, hal::KeyModifier::right_alt)) state |= 0x22;
    if (Has(modifiers, hal::KeyModifier::left_control)) state |= 0x3000;
    if (Has(modifiers, hal::KeyModifier::right_control)) state |= 0x5000;
    if (Has(modifiers, hal::KeyModifier::left_meta)) state |= 0x30000;
    if (Has(modifiers, hal::KeyModifier::right_meta)) state |= 0x50000;
    if (Has(modifiers, hal::KeyModifier::caps_lock)) state |= 0x100000;
    if (Has(modifiers, hal::KeyModifier::num_lock)) state |= 0x200000;
    return state;
}

[[nodiscard]] std::int32_t UnicodeChar(const hal::InputEvent& event) {
    return event.key_symbol >= 0x20 && event.key_symbol <= 0x10ffff
               ? event.key_symbol
               : 0;
}

}  // namespace

std::optional<runtime::AndroidBoundaryInput> MapAndroidInput(
    const hal::InputEvent& event) {
    using Type = runtime::AndroidBoundaryInputType;
    if (event.type == hal::InputEventType::key) {
        return runtime::AndroidBoundaryInput{
            .type = Type::key,
            .code = AndroidKeyCode(event.code),
            .pressed = event.pressed,
            .device_id = static_cast<std::int32_t>(event.device_id),
            .scan_code = event.code,
            .unicode_char = UnicodeChar(event),
            .meta_state = AndroidMetaState(event.key_modifiers),
            .repeat_count = event.repeat ? 1 : 0,
            .event_time_ms = static_cast<std::int64_t>(
                event.timestamp_ns / 1'000'000U),
        };
    }
    if (event.type == hal::InputEventType::pointer_motion ||
        event.type == hal::InputEventType::pointer_button) {
        return runtime::AndroidBoundaryInput{
            .type = event.type == hal::InputEventType::pointer_motion
                        ? Type::pointer_motion
                        : Type::pointer_button,
            .code = event.code,
            .x = event.x,
            .y = event.y,
            .pressed = event.pressed,
        };
    }
    return std::nullopt;
}

runtime::AndroidBoundaryInput MapAndroidTouch(const input::TouchSnapshot& touch) {
    if (touch.points.empty() || touch.points.size() > 16 ||
        touch.changed_index >= touch.points.size()) {
        throw std::invalid_argument("invalid touch snapshot pointer count or index");
    }
    runtime::AndroidBoundaryInput result;
    result.type = runtime::AndroidBoundaryInputType::pointer_motion;
    result.source = 0x1002;
    result.meta_state = AndroidMetaState(touch.modifiers);
    const auto index = static_cast<std::int32_t>(touch.changed_index);
    switch (touch.phase) {
    case input::TouchPhase::down: result.action = touch.points.size() == 1 ? 0 : 5 | (index << 8); break;
    case input::TouchPhase::move: result.action = 2; break;
    case input::TouchPhase::up: result.action = touch.points.size() == 1 ? 1 : 6 | (index << 8); break;
    case input::TouchPhase::cancel: result.action = 3; break;
    }
    for (const auto& point : touch.points) {
        runtime::AndroidInputPointer pointer;
        pointer.id = point.id;
        pointer.axes[0] = point.x; pointer.axes[1] = point.y; pointer.axes[2] = point.pressure;
        result.pointers.push_back(pointer);
    }
    return runtime::NormalizeAndroidInput(std::move(result));
}

std::optional<runtime::AndroidBoundaryInput> AndroidInputTimeline::Stamp(
    runtime::AndroidBoundaryInput input, std::int64_t now_ns) {
    if (now_ns < last_time_ns_) throw std::invalid_argument("input Clock moved backwards");
    input = runtime::NormalizeAndroidInput(std::move(input));
    const auto old_time = input.event_time_ns;
    for (auto& sample : input.history) {
        const auto age = old_time - sample.event_time_ns;
        if (age > now_ns) throw std::invalid_argument("input history predates guest Clock epoch");
        sample.event_time_ns = now_ns - age;
    }
    input.event_time_ns = now_ns;
    input.event_time_ms = now_ns / 1000000;
    if (input.type == runtime::AndroidBoundaryInputType::key) {
        const auto key = std::pair{input.device_id, input.code};
        const auto found = keys_.find(key);
        input.down_time_ns = found == keys_.end() ? now_ns : found->second.down_time_ns;
        input.pressed = input.action == 0;
        if (input.pressed) {
            if (found != keys_.end()) {
                if (found->second.repeat_count == std::numeric_limits<std::int32_t>::max())
                    throw std::overflow_error("key repeat count overflow");
                input.repeat_count = found->second.repeat_count + 1;
            }
            if (keys_.size() >= 256 && found == keys_.end())
                throw std::runtime_error("pressed key budget exhausted");
            keys_[key] = input;
        } else keys_.erase(key);
    } else {
        const auto action = input.action & 0xff;
        const auto found = motions_.find(input.device_id);
        if (action != 0 && found == motions_.end()) return std::nullopt;
        input.down_time_ns = action == 0 ? now_ns : found->second.down_time_ns;
        if (action == 1 || action == 3) motions_.erase(input.device_id);
        else {
            if (motions_.size() >= 16 && found == motions_.end())
                throw std::runtime_error("active touch device budget exhausted");
            auto remaining = input;
            if (action == 6) remaining.pointers.erase(remaining.pointers.begin() + (input.action >> 8));
            motions_[input.device_id] = std::move(remaining);
        }
    }
    last_time_ns_ = now_ns;
    return input;
}

std::vector<runtime::AndroidBoundaryInput> AndroidInputTimeline::Cancel(std::int64_t now_ns) {
    if (now_ns < last_time_ns_) throw std::invalid_argument("input Clock moved backwards");
    std::vector<runtime::AndroidBoundaryInput> result;
    for (const auto& [_, state] : motions_) {
        auto input = state;
        input.action = 3;
        input.history.clear();
        input.event_time_ns = now_ns; input.event_time_ms = now_ns / 1000000;
        result.push_back(runtime::NormalizeAndroidInput(std::move(input)));
    }
    for (const auto& [_, state] : keys_) {
        auto input = state;
        input.action = 1; input.pressed = false; input.flags |= 0x20;
        input.event_time_ns = now_ns; input.event_time_ms = now_ns / 1000000;
        result.push_back(runtime::NormalizeAndroidInput(std::move(input)));
    }
    motions_.clear(); keys_.clear();
    last_time_ns_ = now_ns;
    return result;
}

}  // namespace ogplay::session
