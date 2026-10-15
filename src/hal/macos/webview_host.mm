#define WEBVIEW_HEADER
#include <webview/webview.h>
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <WebKit/WebKit.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include "ogplay/app/json.h"
#include "ogplay/app/logging.h"
#include "ogplay/hal/webview_host.h"

// Exact-address navigation allowlist; same-document fragments stay on the page.
@interface OGPlayNavigationGuard : NSObject <WKNavigationDelegate>
@property(nonatomic, copy) NSString* allowed;
@end
@implementation OGPlayNavigationGuard
- (void)webView:(WKWebView*)view
    decidePolicyForNavigationAction:(WKNavigationAction*)action
                    decisionHandler:(void (^)(WKNavigationActionPolicy))handler {
    NSURLComponents* target = [NSURLComponents componentsWithURL:action.request.URL resolvingAgainstBaseURL:NO];
    target.fragment = nil;
    handler([target.URL.absoluteString isEqualToString:self.allowed] ? WKNavigationActionPolicyAllow
                                                                    : WKNavigationActionPolicyCancel);
}
@end

// Menu actions that must end webview_run normally instead of exiting the process.
@interface OGPlayMenuTarget : NSObject
@property(nonatomic) void (*quit)(void*);
@property(nonatomic) void* context;
- (void)quitLauncher:(id)sender;
@end
@implementation OGPlayMenuTarget
- (void)quitLauncher:(id)sender {
    if (self.quit) self.quit(self.context);
}
@end

namespace ogplay::hal {
namespace {
void CheckWeb(webview_error_t result) {
    if (result != WEBVIEW_ERROR_OK) throw std::runtime_error("WebView operation failed: " + std::to_string(result));
}
NSString* Text(std::string_view value) {
    return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
}
NSURL* FileUrl(const std::filesystem::path& path, bool directory) {
    return [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()] isDirectory:directory];
}
WKWebView* Browser(webview_t view) {
    return (__bridge WKWebView*)webview_get_native_handle(view, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
}
NSWindow* Window(webview_t view) {
    return (__bridge NSWindow*)webview_get_native_handle(view, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
}
NSMenuItem* Item(NSString* title, SEL action, NSString* key) {
    return [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
}

class NativeWebView final : public WebViewHost {
public:
    NativeWebView(WebViewHostOptions options, WebViewHostCallbacks callbacks, app::Logger& logger)
        : options_(std::move(options)), callbacks_(std::move(callbacks)), logger_(logger) {}

    ~NativeWebView() override {
        [timer_ invalidate];
        dashboards_.clear();
        key_editors_.clear();
        if (view_) webview_destroy(view_);
    }

    int Run() override {
        const auto gui_root = options_.document.parent_path();
        if (!std::filesystem::is_regular_file(gui_root / "manifest.json") ||
            !std::filesystem::is_regular_file(gui_root / "index.html"))
            throw std::runtime_error("GUI Web UI is missing; build webui and stage data/webui/gui");
        view_ = webview_create(0, nullptr);
        if (!view_) throw std::runtime_error("Cannot create the system WKWebView");
        CheckWeb(webview_set_title(view_, "OGPlay"));
        CheckWeb(webview_set_size(view_, 960, 640, WEBVIEW_HINT_MIN));
        CheckWeb(webview_set_size(view_, 1280, 800, WEBVIEW_HINT_NONE));
        InstallMenu();
        NSURL* document = FileUrl(gui_root / "index.html", false);
        guard_ = [OGPlayNavigationGuard new];
        guard_.allowed = document.absoluteString;
        WKWebView* browser = Browser(view_);
        if (!browser) throw std::runtime_error("WKWebView is unavailable");
        browser.navigationDelegate = guard_;
        CheckWeb(webview_bind(view_, "rpc", &NativeWebView::Rpc, this));
        const auto smoke = options_.smoke_responses.value_or(0);
        CheckWeb(webview_init(view_, ("window.__ogplaySmoke=" + std::to_string(smoke) + ";").c_str()));
        // Polling continues during window tracking and sheets.
        timer_ = [NSTimer timerWithTimeInterval:0.1 repeats:YES block:^(NSTimer*) { Tick(); }];
        [[NSRunLoop mainRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
        // Read access stays within the bundled GUI directory.
        [browser loadFileURL:document allowingReadAccessToURL:FileUrl(gui_root, true)];
        // Launches not routed through LaunchServices (terminal jobs, CTest) start inactive;
        // an occluded window pauses WebKit rendering and animation frames.
        [Window(view_) makeKeyAndOrderFront:nil];
        [Window(view_) orderFrontRegardless];
        [NSApp activateIgnoringOtherApps:YES];
        CheckWeb(webview_run(view_));
        [timer_ invalidate];
        timer_ = nil;
        if (options_.smoke_responses && (lists_ < *options_.smoke_responses || !smoke_capture_complete_))
            throw std::runtime_error("GUI closed before WebView load and library.list smoke completed");
        logger_.Write(app::LogLevel::info, "frontend.gui", "WebView shell stopped", {}, {{"library_list_calls", lists_}});
        return 0;
    }
    void Evaluate(std::string_view script) override {
        CheckWeb(webview_eval(view_, std::string(script).c_str()));
    }
    void RecordSmokeResponse() override { ++lists_; }
    void Minimize() override { [Window(view_) miniaturize:nil]; }
    void OpenDashboard(std::string_view instance, std::uint16_t port) override {
        if (!port || instance.empty()) throw std::invalid_argument("Dashboard instance and port are required");
        OpenLoopbackWindow(dashboards_, instance, "http://127.0.0.1:" + std::to_string(port) + "/dash/",
                           "OGPlay Dashboard · " + std::string(instance), {});
    }
    void CloseDashboard(std::string_view instance) override { CloseLoopbackWindow(dashboards_, instance); }
    void OpenKeyEditor(std::string_view instance, std::uint16_t port, std::string_view token) override {
        if (!port || instance.empty()) throw std::invalid_argument("key editor instance and port are required");
        if (token.size() < 16 || !std::all_of(token.begin(), token.end(), [](const char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }))
            throw std::invalid_argument("key editor token must be hexadecimal");
        // The page reads its token from session storage; the address carries no secret.
        OpenLoopbackWindow(key_editors_, instance, "http://127.0.0.1:" + std::to_string(port) + "/controls/",
                           "OGPlay 按键设置 · " + std::string(instance),
                           "try{sessionStorage.setItem('ogplay.controls.token','" + std::string(token) + "')}catch(e){}");
    }
    void CloseKeyEditor(std::string_view instance) override { CloseLoopbackWindow(key_editors_, instance); }
    std::future<std::optional<std::filesystem::path>> PickPath(bool directory) override {
        auto promise = std::make_shared<std::promise<std::optional<std::filesystem::path>>>();
        auto result = promise->get_future();
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = !directory;
        panel.canChooseDirectories = directory;
        panel.allowsMultipleSelection = NO;
        panel.canCreateDirectories = directory;
        panel.message = directory ? @"选择游戏数据包目录" : @"选择 APK";
        if (!directory) {
            UTType* apk = [UTType typeWithFilenameExtension:@"apk"];
            if (apk) panel.allowedContentTypes = @[apk];
        }
        // The sheet completes asynchronously on the main thread; the RPC callback never blocks.
        void (^done)(NSModalResponse) = ^(NSModalResponse response) {
            if (response != NSModalResponseOK || panel.URLs.count == 0) { promise->set_value(std::nullopt); return; }
            promise->set_value(std::filesystem::path(panel.URLs.firstObject.fileSystemRepresentation));
        };
        if (NSWindow* owner = Window(view_)) [panel beginSheetModalForWindow:owner completionHandler:done];
        else done([panel runModal]);
        return result;
    }

private:
    struct LoopbackWindow final {
        webview_t view{};
        OGPlayNavigationGuard* guard{};
        LoopbackWindow() = default;
        LoopbackWindow(const LoopbackWindow&) = delete;
        LoopbackWindow& operator=(const LoopbackWindow&) = delete;
        ~LoopbackWindow() { if (view) webview_destroy(view); }
    };
    using LoopbackWindows = std::map<std::string, std::unique_ptr<LoopbackWindow>>;

    static void Rpc(const char* id, const char* args, void* user) noexcept {
        auto& self = *static_cast<NativeWebView*>(user);
        try {
            app::JsonParseError error;
            auto document = app::JsonDocument::ParseStrict(args, error);
            const auto root = document ? document->Root() : app::JsonValue{};
            const auto arg = root.Element(0);
            const auto text = arg ? arg->String() : std::nullopt;
            if (!root.IsArray() || root.Size() != 1 || !text) throw std::invalid_argument("rpc requires one JSON string");
            const auto response = self.callbacks_.request(*text);
            CheckWeb(webview_return(self.view_, id, 0, response.c_str()));
        } catch (const std::exception& error) {
            app::JsonWriter writer;
            const auto result = writer.Object();
            writer.AddString(result, "message", error.what());
            writer.AddString(result, "next_step", "检查 GUI 日志和前端制品后重试。");
            static_cast<void>(webview_return(self.view_, id, 1, writer.Serialize(result).c_str()));
        }
    }
    static void Quit(void* context) {
        auto& self = *static_cast<NativeWebView*>(context);
        self.CloseAllRuntimeWindows();
        static_cast<void>(webview_terminate(self.view_));
    }
    // webview does not create a main menu; without Edit items text fields lose Cmd+C/V.
    void InstallMenu() {
        menu_target_ = [OGPlayMenuTarget new];
        menu_target_.quit = &NativeWebView::Quit;
        menu_target_.context = this;
        NSMenu* bar = [NSMenu new];
        NSMenuItem* app_item = [NSMenuItem new];
        NSMenu* app_menu = [[NSMenu alloc] initWithTitle:@"OGPlay"];
        [app_menu addItem:Item(@"隐藏 OGPlay", @selector(hide:), @"h")];
        NSMenuItem* others = Item(@"隐藏其他", @selector(hideOtherApplications:), @"h");
        others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
        [app_menu addItem:others];
        [app_menu addItem:Item(@"全部显示", @selector(unhideAllApplications:), @"")];
        [app_menu addItem:[NSMenuItem separatorItem]];
        NSMenuItem* quit = Item(@"退出 OGPlay", @selector(quitLauncher:), @"q");
        quit.target = menu_target_;
        [app_menu addItem:quit];
        app_item.submenu = app_menu;
        [bar addItem:app_item];
        NSMenuItem* edit_item = [NSMenuItem new];
        NSMenu* edit = [[NSMenu alloc] initWithTitle:@"编辑"];
        [edit addItem:Item(@"撤销", @selector(undo:), @"z")];
        NSMenuItem* redo = Item(@"重做", @selector(redo:), @"z");
        redo.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        [edit addItem:redo];
        [edit addItem:[NSMenuItem separatorItem]];
        [edit addItem:Item(@"剪切", @selector(cut:), @"x")];
        [edit addItem:Item(@"拷贝", @selector(copy:), @"c")];
        [edit addItem:Item(@"粘贴", @selector(paste:), @"v")];
        [edit addItem:Item(@"全选", @selector(selectAll:), @"a")];
        edit_item.submenu = edit;
        [bar addItem:edit_item];
        NSMenuItem* window_item = [NSMenuItem new];
        NSMenu* window_menu = [[NSMenu alloc] initWithTitle:@"窗口"];
        [window_menu addItem:Item(@"最小化", @selector(performMiniaturize:), @"m")];
        [window_menu addItem:Item(@"关闭", @selector(performClose:), @"w")];
        window_item.submenu = window_menu;
        [bar addItem:window_item];
        NSApp.mainMenu = bar;
        NSApp.windowsMenu = window_menu;
    }
    void CloseAllRuntimeWindows() {
        const auto previous = window_operation_; window_operation_ = true;
        dashboards_.clear(); key_editors_.clear();
        window_operation_ = previous;
    }
    void Tick() {
        if (polling_ || window_operation_) return;
        polling_ = true;
        struct Reset { bool& flag; ~Reset() { flag = false; } } reset{polling_};
        try {
            // Closing the launcher window ends the shell even while runtime windows remain.
            if (!Window(view_)) { CloseAllRuntimeWindows(); static_cast<void>(webview_terminate(view_)); return; }
            callbacks_.poll();
            if (options_.smoke_responses && lists_ >= *options_.smoke_responses && !smoke_capture_requested_)
                CaptureSmoke();
        } catch (const std::exception& error) {
            logger_.Write(app::LogLevel::error, "frontend.gui", "process polling failed", {}, {{"reason", std::string(error.what())}});
        }
    }
    void CaptureSmoke() {
        smoke_capture_requested_ = true;
        NSString* target = [NSString stringWithUTF8String:options_.smoke_image.c_str()];
        [Browser(view_) takeSnapshotWithConfiguration:[WKSnapshotConfiguration new]
                                    completionHandler:^(NSImage* image, NSError*) {
            NSData* png{};
            if (image) {
                NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithData:image.TIFFRepresentation];
                png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            }
            smoke_capture_complete_ = png && [png writeToFile:target atomically:YES];
            static_cast<void>(webview_terminate(view_));
        }];
    }
    // Independent runtime windows on a game's loopback service; never bound to the launcher rpc.
    void OpenLoopbackWindow(LoopbackWindows& windows, std::string_view instance, const std::string& url,
                            const std::string& title, const std::string& init_script) {
        if (window_operation_) throw std::runtime_error("Runtime window operation is already in progress");
        const auto previous = window_operation_; window_operation_ = true;
        struct Reset { bool& flag; bool previous; ~Reset() { flag = previous; } } reset{window_operation_, previous};
        if (const auto found = windows.find(std::string(instance)); found != windows.end()) {
            if (NSWindow* window = Window(found->second->view)) {
                [window deminiaturize:nil];
                [window makeKeyAndOrderFront:nil];
                [NSApp activateIgnoringOtherApps:YES];
                return;
            }
            windows.erase(found);
        }
        auto child = std::make_unique<LoopbackWindow>();
        child->view = webview_create(0, nullptr);
        if (!child->view) throw std::runtime_error("Cannot create runtime WKWebView window");
        CheckWeb(webview_set_title(child->view, title.c_str()));
        CheckWeb(webview_set_size(child->view, 960, 640, WEBVIEW_HINT_MIN));
        CheckWeb(webview_set_size(child->view, 1280, 800, WEBVIEW_HINT_NONE));
        child->guard = [OGPlayNavigationGuard new];
        child->guard.allowed = Text(url);
        WKWebView* browser = Browser(child->view);
        if (!browser) throw std::runtime_error("Runtime WKWebView is unavailable");
        browser.navigationDelegate = child->guard;
        if (!init_script.empty()) CheckWeb(webview_init(child->view, init_script.c_str()));
        // No webview_bind: runtime content cannot invoke launcher operations.
        CheckWeb(webview_navigate(child->view, url.c_str()));
        [Window(child->view) makeKeyAndOrderFront:nil];
        windows.emplace(instance, std::move(child));
    }
    void CloseLoopbackWindow(LoopbackWindows& windows, std::string_view instance) {
        const auto previous = window_operation_; window_operation_ = true;
        struct Reset { bool& flag; bool previous; ~Reset() { flag = previous; } } reset{window_operation_, previous};
        windows.erase(std::string(instance));
    }

    LoopbackWindows dashboards_;
    LoopbackWindows key_editors_;
    WebViewHostOptions options_;
    WebViewHostCallbacks callbacks_;
    app::Logger& logger_;
    webview_t view_{};
    OGPlayNavigationGuard* guard_{};
    OGPlayMenuTarget* menu_target_{};
    NSTimer* timer_{};
    std::uint64_t lists_{};
    bool window_operation_{};
    bool polling_{};
    bool smoke_capture_requested_{};
    bool smoke_capture_complete_{};
};
}  // namespace

std::unique_ptr<WebViewHost> CreateWebViewHost(WebViewHostOptions options,
    WebViewHostCallbacks callbacks, app::Logger& logger) {
    if (!callbacks.request || !callbacks.poll) throw std::invalid_argument("WebView callbacks are required");
    return std::make_unique<NativeWebView>(std::move(options), std::move(callbacks), logger);
}

void OpenHostDirectory(const std::filesystem::path& path) {
    if (![[NSWorkspace sharedWorkspace] openURL:FileUrl(path, true)]) throw std::runtime_error("cannot open directory");
}

}  // namespace ogplay::hal
