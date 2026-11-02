#pragma once
namespace ogplay::test {
// Sends the standard close action to the first top-level window with this title.
bool CloseWindowTitled(const char* title);
// True when that window is on screen.
bool WindowVisible(const char* title);
// Sends the app menu's quit item (Cmd+Q), the way out once the close button only hides the launcher.
bool QuitFromMenu();
}
