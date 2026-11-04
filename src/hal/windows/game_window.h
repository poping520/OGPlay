#pragma once

#include "ogplay/hal/window_input.h"

namespace ogplay::hal::windows {

// `hwnd` is the game window's HWND, null when the SDL backend has none (appearance is then skipped).
void ApplyGameWindowChrome(void* hwnd) noexcept;

// Modal dialog over the game window, on the calling thread that owns it. True only when the user chose to close;
// throws std::runtime_error when the dialog cannot be shown.
[[nodiscard]] bool ShowCloseConfirmation(void* hwnd, const CloseConfirmation& text);

}  // namespace ogplay::hal::windows
