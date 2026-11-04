#pragma once

namespace ogplay::hal::windows {
// Shared by the native desktop entry and its main WebView window only.
inline constexpr wchar_t kLauncherMutex[] = L"Local\\OGPlay.Launcher.Instance.v1";
inline constexpr wchar_t kLauncherWindowProperty[] = L"OGPlay.Launcher.Window.v1";
inline constexpr wchar_t kLauncherActivateMessage[] = L"OGPlay.Launcher.Activate.v1";
}  // namespace ogplay::hal::windows
