#include "ogplay/frontend/host_shortcuts.h"

#include <algorithm>

namespace ogplay::frontend {
namespace {

// USB HID usage ids (SDL scancodes).
constexpr std::int32_t kF1 = 58, kF2 = 59;
constexpr std::int32_t kLeftCtrl = 224, kLeftMeta = 227, kRightCtrl = 228, kRightMeta = 231;

[[nodiscard]] bool Has(const std::uint32_t modifiers, const hal::KeyModifier modifier) {
    return (modifiers & static_cast<std::uint32_t>(modifier)) != 0U;
}

}  // namespace

HostKeyDecision HostShortcutFilter::Filter(const hal::InputEvent& event) {
    if (event.type != hal::InputEventType::key) return {};
    const auto code = event.code;
    const auto held = std::ranges::find(held_, code);
    if (!event.pressed) {
        if (held == held_.end()) return {};
        held_.erase(held);
        return {true, HostShortcut::none};
    }
    // A repeat never changes owner: guest keys keep repeating into the guest under Ctrl.
    if (held != held_.end() || event.repeat) return {held != held_.end(), HostShortcut::none};
    using Modifier = hal::KeyModifier;
    const auto modifiers = event.key_modifiers;
    const bool chord = code != kLeftCtrl && code != kRightCtrl &&
        (Has(modifiers, Modifier::left_control) || Has(modifiers, Modifier::right_control) ||
         Has(modifiers, Modifier::left_meta) || Has(modifiers, Modifier::right_meta));
    if (!chord && code != kF1 && code != kF2 && code != kLeftMeta && code != kRightMeta) return {};
    held_.push_back(code);
    if (chord) return {true, HostShortcut::none};
    if (code == kF1) return {true, HostShortcut::toggle_hints};
    if (code == kF2) return {true, HostShortcut::open_key_editor};
    return {true, HostShortcut::none};
}

void HostShortcutFilter::Reset() noexcept { held_.clear(); }

}  // namespace ogplay::frontend
