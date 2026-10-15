#import <Cocoa/Cocoa.h>
#include "gui_window_test_support.h"
namespace ogplay::test {
bool CloseWindowTitled(const char* title) {
    NSString* wanted = [NSString stringWithUTF8String:title];
    for (NSWindow* window in NSApp.windows) {
        if ([window.title isEqualToString:wanted]) {
            [window performClose:nil];
            return true;
        }
    }
    return false;
}
}
