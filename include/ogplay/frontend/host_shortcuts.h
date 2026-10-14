#pragma once

#include <cstdint>
#include <vector>

#include "ogplay/hal/window_input.h"

namespace ogplay::frontend {

enum class HostShortcut : std::uint8_t { none, toggle_hints, open_key_editor };

struct HostKeyDecision final {
    bool consumed{};
    HostShortcut shortcut{HostShortcut::none};
};

// Keys reserved for the host (ADR-0134): F1, F2, Cmd/Win and any key pressed while Ctrl or
// Cmd/Win is held. A reserved press keeps its repeats and release on the host even when the
// modifier is let go first; keys that reached the guest keep their release there.
class HostShortcutFilter final {
public:
    [[nodiscard]] HostKeyDecision Filter(const hal::InputEvent& event);
    // Focus loss and input resets drop held host keys; the host never sees their release.
    void Reset() noexcept;

private:
    std::vector<std::int32_t> held_;
};

}  // namespace ogplay::frontend
