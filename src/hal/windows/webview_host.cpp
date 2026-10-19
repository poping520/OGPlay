#define WEBVIEW_HEADER
#define NOMINMAX
#include <webview/webview.h>
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <wrl.h>
#include <WebView2.h>
#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <stdexcept>
#include <utility>
#include "ogplay/app/json.h"
#include "ogplay/app/logging.h"
#include "ogplay/hal/webview_host.h"

namespace ogplay::hal {
namespace {
std::string PathUtf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
void Check(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("WebView2 operation failed: " + std::to_string(result));
}
void CheckWeb(webview_error_t result) {
    if (result != WEBVIEW_ERROR_OK) throw std::runtime_error("WebView operation failed: " + std::to_string(result));
}

void SetApplicationIcon(webview_t view) {
    const auto window = static_cast<HWND>(webview_get_native_handle(view, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW));
    const auto module = GetModuleHandleW(nullptr);
    // LR_SHARED icons belong to the module and remain valid for every WebView window.
    const auto large_icon = static_cast<HICON>(LoadImageW(module, L"OGPLAY_ICON", IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    const auto small_icon = static_cast<HICON>(LoadImageW(module, L"OGPLAY_ICON", IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!window || !large_icon || !small_icon) throw std::runtime_error("Cannot load OGPlay application icon");
    SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon));
    SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
}

// The host owns the top-level HWND; webview owns only its child widget/controller.
class WebViewWindow final {
public:
    explicit WebViewWindow(bool main_window) : main_window_(main_window) {
        static const auto window_class = [] {
            WNDCLASSEXW descriptor{};
            descriptor.cbSize = sizeof(descriptor);
            descriptor.hInstance = GetModuleHandleW(nullptr);
            descriptor.lpfnWndProc = &WebViewWindow::WindowProc;
            descriptor.lpszClassName = L"OGPlayWebViewHost";
            descriptor.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            descriptor.hbrBackground = CreateSolidBrush(RGB(9, 12, 22)); // splash #090c16
            if (!descriptor.hbrBackground) throw std::runtime_error("Cannot create GUI background brush");
            const auto atom = RegisterClassExW(&descriptor);
            if (!atom) {
                DeleteObject(descriptor.hbrBackground);
                throw std::runtime_error("Cannot register GUI window class");
            }
            // The registered class and its brush live for the process lifetime.
            return atom;
        }();
        POINT cursor{};
        GetCursorPos(&cursor);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor))
            throw std::runtime_error("Cannot query GUI monitor");
        const auto bounds = FrameSize(1280, 800, GetDpiForSystem());
        window_ = CreateWindowExW(0, MAKEINTRESOURCEW(window_class), L"OGPlay",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, monitor.rcWork.left, monitor.rcWork.top,
            bounds.cx, bounds.cy, nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (!window_) throw std::runtime_error("Cannot create GUI window");
        const auto actual = FrameSize(1280, 800, GetDpiForWindow(window_));
        SetWindowPos(window_, nullptr,
            monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - actual.cx) / 2,
            monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - actual.cy) / 2,
            actual.cx, actual.cy, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ~WebViewWindow() {
        if (window_) KillTimer(window_, kShowTimer);
        const auto view = std::exchange(view_, nullptr);
        if (view) webview_destroy(view);
        if (window_) DestroyWindow(window_);
    }
    WebViewWindow(const WebViewWindow&) = delete;
    WebViewWindow& operator=(const WebViewWindow&) = delete;

    void Initialize() {
        view_ = webview_create(0, &window_);
        if (!view_) throw std::runtime_error("Cannot create WebView2; install Microsoft Edge WebView2 Runtime");
        SetApplicationIcon(view_);
        auto* controller = static_cast<ICoreWebView2Controller*>(
            webview_get_native_handle(view_, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
        if (!controller) throw std::runtime_error("WebView2 controller is unavailable");
        Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;
        Check(controller->QueryInterface(IID_PPV_ARGS(&controller2)));
        Check(controller2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255, 9, 12, 22}));
        ResizeWidget();
    }
    void Navigate(const std::string& url) {
        auto* controller = static_cast<ICoreWebView2Controller*>(
            webview_get_native_handle(view_, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
        Microsoft::WRL::ComPtr<ICoreWebView2> browser;
        Check(controller->get_CoreWebView2(&browser));
        EventRegistrationToken token{};
        Check(browser->add_NavigationCompleted(Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [window = window_](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                // Completion includes navigation failures; both must reveal the window.
                PostMessageW(window, kNavigationReady, 0, 0);
                return S_OK;
            }).Get(), &token));
        if (!SetTimer(window_, kShowTimer, 2000, nullptr))
            throw std::runtime_error("Cannot create GUI visibility timer");
        CheckWeb(webview_navigate(view_, url.c_str()));
    }
    webview_t View() const { return view_; }
    void Restore() {
        if (!shown_) return; // A repeated open must still wait for navigation/the timeout.
        ShowWindow(window_, SW_RESTORE);
        SetForegroundWindow(window_);
    }
    bool IsOpen() const { return window_ && IsWindow(window_); }
private:
    static constexpr UINT_PTR kShowTimer = 1;
    static constexpr UINT kNavigationReady = WM_APP + 1;
    static SIZE FrameSize(int width, int height, UINT dpi) {
        RECT bounds{0, 0, MulDiv(width, static_cast<int>(dpi), 96), MulDiv(height, static_cast<int>(dpi), 96)};
        if (!AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE, 0, dpi))
            throw std::runtime_error("Cannot calculate GUI window size");
        return {bounds.right - bounds.left, bounds.bottom - bounds.top};
    }
    void ResizeWidget() const {
        if (!view_ || !window_) return;
        const auto widget = static_cast<HWND>(webview_get_native_handle(view_, WEBVIEW_NATIVE_HANDLE_KIND_UI_WIDGET));
        RECT bounds{};
        if (widget && GetClientRect(window_, &bounds))
            MoveWindow(widget, 0, 0, bounds.right, bounds.bottom, TRUE);
    }
    void Show() {
        if (shown_ || !view_) return;
        shown_ = true;
        KillTimer(window_, kShowTimer);
        ShowWindow(window_, SW_SHOW);
        SetForegroundWindow(window_);
    }
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) noexcept {
        auto* self = reinterpret_cast<WebViewWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<WebViewWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wp, lp);
        switch (message) {
        case WM_SIZE:
            self->ResizeWidget();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE && self->view_) {
                auto* controller = static_cast<ICoreWebView2Controller*>(webview_get_native_handle(
                    self->view_, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
                if (controller) static_cast<void>(controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC));
            }
            return 0;
        case WM_GETMINMAXINFO: {
            // Minimum client size follows the same DPI/frame conversion as the initial size.
            RECT bounds{0, 0, MulDiv(960, static_cast<int>(GetDpiForWindow(window)), 96),
                MulDiv(640, static_cast<int>(GetDpiForWindow(window)), 96)};
            if (AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE, 0, GetDpiForWindow(window)))
                reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {bounds.right - bounds.left, bounds.bottom - bounds.top};
            return 0;
        }
        case WM_DPICHANGED: {
            const auto* bounds = reinterpret_cast<RECT*>(lp);
            SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
                bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case kNavigationReady:
            self->Show();
            return 0;
        case WM_TIMER:
            if (wp == kShowTimer) { self->Show(); return 0; }
            break;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            KillTimer(window, kShowTimer);
            if (self->main_window_ && self->view_) static_cast<void>(webview_terminate(self->view_));
            return 0;
        case WM_NCDESTROY:
            self->window_ = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        }
        return DefWindowProcW(window, message, wp, lp);
    }
    HWND window_{};
    webview_t view_{};
    bool main_window_{};
    bool shown_{};
};

class NativeWebView final : public WebViewHost {
public:
    NativeWebView(WebViewHostOptions options, WebViewHostCallbacks callbacks, app::Logger& logger)
        : options_(std::move(options)), callbacks_(std::move(callbacks)), logger_(logger) {}

    ~NativeWebView() {
        if (active_ == this) active_ = nullptr;
        if (timer_) KillTimer(nullptr, timer_);
        dashboards_.clear();
        key_editors_.clear();
        main_window_.reset();
        if (com_initialized_) CoUninitialize();
    }

    int Run() override {
        const auto gui_root = options_.document.parent_path();
        if (!std::filesystem::is_regular_file(gui_root / "manifest.json") ||
            !std::filesystem::is_regular_file(gui_root / "index.html"))
            throw std::runtime_error("GUI Web UI is missing; build webui and stage data/webui/gui");
        Check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        com_initialized_ = true;
        // External-window webview creation leaves DPI awareness and COM to the host.
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        main_window_ = std::make_unique<WebViewWindow>(true);
        main_window_->Initialize();
        view_ = main_window_->View();
        CheckWeb(webview_set_title(view_, "OGPlay"));
        wchar_t uri[32768]{};
        DWORD length = static_cast<DWORD>(std::size(uri));
        Check(UrlCreateFromPathW((gui_root / "index.html").c_str(), uri, &length, 0));
        const std::wstring allowed(uri);
        auto* controller = static_cast<ICoreWebView2Controller*>(
            webview_get_native_handle(view_, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
        if (!controller) throw std::runtime_error("WebView2 controller is unavailable");
        Microsoft::WRL::ComPtr<ICoreWebView2> browser;
        Check(controller->get_CoreWebView2(&browser));
        EventRegistrationToken token{};
        Check(browser->add_NavigationStarting(Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
            [allowed](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR target{};
                const auto result = args->get_Uri(&target);
                const bool permitted = SUCCEEDED(result) && target && allowed == target;
                CoTaskMemFree(target);
                return args->put_Cancel(permitted ? FALSE : TRUE);
            }).Get(), &token));
        Check(browser->add_NewWindowRequested(Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                return args->put_Handled(TRUE);
            }).Get(), &token));
        CheckWeb(webview_bind(view_, "rpc", &NativeWebView::Rpc, this));
        const auto smoke = options_.smoke_responses.value_or(0);
        CheckWeb(webview_init(view_, ("window.__ogplaySmoke=" + std::to_string(smoke) + ";").c_str()));
        active_ = this;
        timer_ = SetTimer(nullptr, 0, 100, &NativeWebView::Tick);
        if (!timer_) throw std::runtime_error("Cannot create GUI process polling timer");
        const auto url = PathUtf8(std::filesystem::path(allowed));
        main_window_->Navigate(url);
        CheckWeb(webview_run(view_));
        active_ = nullptr;
        if (options_.smoke_responses && (lists_ < *options_.smoke_responses || !smoke_capture_complete_))
            throw std::runtime_error("GUI closed before WebView load and library.list smoke completed");
        logger_.Write(app::LogLevel::info, "frontend.gui", "WebView shell stopped", {}, {{"library_list_calls", lists_}});
        return 0;
    }
    void Evaluate(std::string_view script) override {
        CheckWeb(webview_eval(view_, std::string(script).c_str()));
    }
    void RecordSmokeResponse() override { ++lists_; }
    void Minimize() override {
        const auto window = static_cast<HWND>(webview_get_native_handle(view_, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW));
        ShowWindow(window, SW_MINIMIZE);
    }
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
        // The page reads its token from session storage, so the address carries no secret
        // and the navigation allowlist stays an exact URL.
        OpenLoopbackWindow(key_editors_, instance, "http://127.0.0.1:" + std::to_string(port) + "/controls/",
                           "OGPlay 按键设置 · " + std::string(instance),
                           "try{sessionStorage.setItem('ogplay.controls.token','" + std::string(token) + "')}catch(e){}");
    }
    void CloseKeyEditor(std::string_view instance) override { CloseLoopbackWindow(key_editors_, instance); }
    std::future<std::optional<std::filesystem::path>> PickPath(bool directory) override {
        const auto owner = static_cast<HWND>(webview_get_native_handle(view_, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW));
        // A separate STA keeps modal Shell UI out of the WebView message callback.
        return std::async(std::launch::async, [owner, directory]() -> std::optional<std::filesystem::path> {
            Check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
            struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
            Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
            Check(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
            FILEOPENDIALOGOPTIONS flags{};
            Check(dialog->GetOptions(&flags));
            Check(dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                (directory ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST)));
            if (!directory) {
                const COMDLG_FILTERSPEC filters[]{{L"Android APK", L"*.apk"}};
                Check(dialog->SetFileTypes(1, filters));
            }
            Check(dialog->SetTitle(directory ? L"选择游戏数据包目录" : L"选择 APK"));
            const auto result = dialog->Show(owner);
            if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::nullopt;
            Check(result);
            Microsoft::WRL::ComPtr<IShellItem> item;
            Check(dialog->GetResult(&item));
            PWSTR path{};
            Check(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
            const std::filesystem::path selected(path);
            CoTaskMemFree(path);
            return selected;
        });
    }
private:
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
    static void CALLBACK Tick(HWND, UINT, UINT_PTR, DWORD) noexcept {
        if (!active_) return;
        auto& self = *active_;
        if (self.polling_ || self.window_operation_) return;
        self.polling_ = true;
        struct Reset { bool& flag; ~Reset() { flag = false; } } reset{self.polling_};
        try {
            const auto main_window = static_cast<HWND>(webview_get_native_handle(self.view_, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW));
            if (!main_window || !IsWindow(main_window)) {
                self.dashboards_.clear(); self.key_editors_.clear(); static_cast<void>(webview_terminate(self.view_)); return;
            }
            self.callbacks_.poll();
            if (self.options_.smoke_responses && self.lists_ >= *self.options_.smoke_responses && !self.smoke_capture_requested_)
                self.CaptureSmoke();
        } catch (const std::exception& error) {
            self.logger_.Write(app::LogLevel::error, "frontend.gui", "process polling failed", {}, {{"reason", std::string(error.what())}});
        }
    }
    void CaptureSmoke() {
        auto* controller = static_cast<ICoreWebView2Controller*>(
            webview_get_native_handle(view_, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
        Microsoft::WRL::ComPtr<ICoreWebView2> browser;
        Check(controller->get_CoreWebView2(&browser));
        Microsoft::WRL::ComPtr<IStream> stream;
        Check(SHCreateStreamOnFileEx(options_.smoke_image.c_str(),
              STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, FILE_ATTRIBUTE_NORMAL,
              TRUE, nullptr, &stream));
        smoke_capture_requested_ = true;
        Check(browser->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.Get(),
            Microsoft::WRL::Callback<ICoreWebView2CapturePreviewCompletedHandler>(
                [this, stream](HRESULT status) -> HRESULT {
                    smoke_capture_complete_ = SUCCEEDED(status);
                    static_cast<void>(webview_terminate(view_));
                    return S_OK;
                }).Get()));
    }
    using DashboardWindow = std::unique_ptr<WebViewWindow>;
    using LoopbackWindows = std::map<std::string, DashboardWindow>;
    // Independent runtime windows on a game's loopback service; never bound to the launcher rpc.
    void OpenLoopbackWindow(LoopbackWindows& windows, std::string_view instance, const std::string& url,
                            const std::string& title, const std::string& init_script) {
        if (window_operation_) throw std::runtime_error("Runtime window operation is already in progress");
        const auto previous = window_operation_; window_operation_ = true;
        struct Reset { bool& flag; bool previous; ~Reset() { flag = previous; } } reset{window_operation_, previous};
        auto found = windows.find(std::string(instance));
        if (found != windows.end()) {
            if (found->second->IsOpen()) { found->second->Restore(); return; }
            windows.erase(found);
        }
        auto child = std::make_unique<WebViewWindow>(false);
        child->Initialize();
        CheckWeb(webview_set_title(child->View(), title.c_str()));
        auto* controller = static_cast<ICoreWebView2Controller*>(webview_get_native_handle(child->View(), WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
        if (!controller) throw std::runtime_error("Runtime WebView2 controller unavailable");
        Microsoft::WRL::ComPtr<ICoreWebView2> browser; Check(controller->get_CoreWebView2(&browser));
        const std::wstring allowed(url.begin(), url.end());
        EventRegistrationToken token{};
        Check(browser->add_NavigationStarting(Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
            [allowed](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR target{}; const auto result = args->get_Uri(&target);
                const bool permitted = SUCCEEDED(result) && target && allowed == target;
                CoTaskMemFree(target); return args->put_Cancel(permitted ? FALSE : TRUE);
            }).Get(), &token));
        Check(browser->add_NewWindowRequested(Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT { return args->put_Handled(TRUE); }).Get(), &token));
        if (!init_script.empty()) CheckWeb(webview_init(child->View(), init_script.c_str()));
        // No webview_bind: runtime content cannot invoke launcher operations.
        child->Navigate(url);
        windows.emplace(instance, std::move(child));
    }
    void CloseLoopbackWindow(LoopbackWindows& windows, std::string_view instance) {
        const auto previous = window_operation_; window_operation_ = true;
        struct Reset { bool& flag; bool previous; ~Reset() { flag = previous; } } reset{window_operation_, previous};
        windows.erase(std::string(instance));
    }
    LoopbackWindows dashboards_;
    LoopbackWindows key_editors_;
    inline static NativeWebView* active_{};
    WebViewHostOptions options_;
    WebViewHostCallbacks callbacks_;
    app::Logger& logger_;
    std::unique_ptr<WebViewWindow> main_window_;
    bool com_initialized_{};
    webview_t view_{};
    UINT_PTR timer_{};
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
    const auto result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) throw std::runtime_error("cannot open directory");
}
}  // namespace ogplay::hal
