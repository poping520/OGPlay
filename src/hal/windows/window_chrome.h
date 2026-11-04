#pragma once

#include <windows.h>
#include <dwmapi.h>

namespace ogplay::hal::windows {

// Launcher palette (webui/packages/ui-kit/theme.css) for what the system draws around a window.
inline constexpr COLORREF kChromeCaption = RGB(0x09, 0x0c, 0x16);  // --bg
inline constexpr COLORREF kChromeText = RGB(0xf2, 0xf4, 0xfa);     // --ink
inline constexpr COLORREF kChromeBorder = RGB(0x26, 0x2f, 0x45);   // --line

// Numbered here because older Windows SDKs do not name them.
inline constexpr DWORD kDwmImmersiveDarkModeBefore20H1 = 19;
inline constexpr DWORD kDwmImmersiveDarkMode = 20;
inline constexpr DWORD kDwmCornerPreference = 33;
inline constexpr DWORD kDwmBorderColor = 34;
inline constexpr DWORD kDwmCaptionColor = 35;
inline constexpr DWORD kDwmTextColor = 36;

// Dark title bar in the launcher's colours. Colours need Windows 11 and dark mode Windows 10 1809; an older
// system keeps what it supports, because this is appearance only and must not stop a window from opening.
inline void ApplyWindowChrome(const HWND window) noexcept {
    const BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(window, kDwmImmersiveDarkMode, &dark, sizeof(dark))))
        static_cast<void>(DwmSetWindowAttribute(window, kDwmImmersiveDarkModeBefore20H1, &dark, sizeof(dark)));
    static_cast<void>(DwmSetWindowAttribute(window, kDwmCaptionColor, &kChromeCaption, sizeof(kChromeCaption)));
    static_cast<void>(DwmSetWindowAttribute(window, kDwmTextColor, &kChromeText, sizeof(kChromeText)));
    static_cast<void>(DwmSetWindowAttribute(window, kDwmBorderColor, &kChromeBorder, sizeof(kChromeBorder)));
}

}  // namespace ogplay::hal::windows
