#pragma once

#include <span>

#include "ogplay/hal/window_input.h"

namespace ogplay::hal::windows {

// Title-bar (ICON_SMALL) and taskbar/Alt-Tab (ICON_BIG) icons of one window, each made from the given size
// nearest the system metric at the window's DPI. WM_SETICON does not take ownership, so this keeps the
// HICONs alive until Release (call it after the window is destroyed) or the next Apply.
class WindowIcons final {
public:
    WindowIcons() = default;
    WindowIcons(const WindowIcons&) = delete;
    WindowIcons& operator=(const WindowIcons&) = delete;

    ~WindowIcons() { Release(); }

    // `hwnd` is the window's HWND; throws std::runtime_error when an icon cannot be created.
    void Apply(void* hwnd, std::span<const IconImage> sizes);
    void Release() noexcept;

private:
    void* small_{};
    void* big_{};
};

}  // namespace ogplay::hal::windows
