#import <Cocoa/Cocoa.h>
#include "gui_window_test_support.h"
namespace ogplay::test {
namespace {
NSWindow* WindowTitled(const char* title) {
    NSString* wanted = [NSString stringWithUTF8String:title];
    for (NSWindow* window in NSApp.windows)
        if ([window.title isEqualToString:wanted]) return window;
    return nil;
}
}  // namespace

bool CloseWindowTitled(const char* title) {
    NSWindow* window = WindowTitled(title);
    [window performClose:nil];
    return window != nil;
}

bool WindowVisible(const char* title) {
    return WindowTitled(title).visible;
}

bool QuitFromMenu() {
    NSMenu* app_menu = NSApp.mainMenu.itemArray.firstObject.submenu;
    for (NSMenuItem* item in app_menu.itemArray)
        if ([item.keyEquivalent isEqualToString:@"q"]) return [NSApp sendAction:item.action to:item.target from:item];
    return false;
}
}
