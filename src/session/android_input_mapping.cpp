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

}  // namespace ogplay::session
