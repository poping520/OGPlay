#define WEBVIEW_HEADER
#define NOMINMAX
#include <webview/webview.h>
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <wrl.h>
#include <WebView2.h>
#include <iterator>
#include <map>
#include <stdexcept>
#include <utility>
#include "ogplay/core/json.h"
#include "ogplay/core/logger.h"
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

class NativeWebView final : public WebViewHost {
public:
    NativeWebView(WebViewHostOptions options, WebViewHostCallbacks callbacks, core::Logger& logger)
        : options_(std::move(options)), callbacks_(std::move(callbacks)), logger_(logger) {}

    ~NativeWebView() {
        if (active_ == this) active_ = nullptr;
        if (timer_) KillTimer(nullptr, timer_);
        dashboards_.clear();
        if (view_) webview_destroy(view_);
    }

    int Run() override {
        const auto gui_root = options_.document.parent_path();
        if (!std::filesystem::is_regular_file(gui_root / "manifest.json") ||
            !std::filesystem::is_regular_file(gui_root / "index.html"))
            throw std::runtime_error("GUI Web UI is missing; build webui and stage data/webui/gui");
        view_ = webview_create(0, nullptr);
        if (!view_) throw std::runtime_error("Cannot create WebView2; install Microsoft Edge WebView2 Runtime");
        CheckWeb(webview_set_title(view_, "OGPlay"));
        CheckWeb(webview_set_size(view_, 960, 640, WEBVIEW_HINT_MIN));
        CheckWeb(webview_set_size(view_, 1280, 800, WEBVIEW_HINT_NONE));
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
        CheckWeb(webview_navigate(view_, url.c_str()));
        CheckWeb(webview_run(view_));
        active_ = nullptr;
        if (options_.smoke_responses && (lists_ < *options_.smoke_responses || !smoke_capture_complete_))
            throw std::runtime_error("GUI closed before WebView load and library.list smoke completed");
        logger_.Write(core::LogLevel::info, "frontend.gui", "WebView shell stopped", {}, {{"library_list_calls", lists_}});
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
        if (window_operation_) throw std::runtime_error("Dashboard window operation is already in progress");
        const auto previous = window_operation_; window_operation_ = true;
        struct Reset { bool& flag; bool previous; ~Reset() { flag = previous; } } reset{window_operation_, previous};
        auto found = dashboards_.find(std::string(instance));
        if (found != dashboards_.end()) {
            const auto window = static_cast<HWND>(webview_get_native_handle(found->second.get(), WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW));
            if (window && IsWindow(window)) { ShowWindow(window, SW_RESTORE); SetForegroundWindow(window); return; }
            dashboards_.erase(found);
        }
        const auto url = "http://127.0.0.1:" + std::to_string(port) + "/dash/";
        DashboardWindow child(webview_create(0, nullptr));
        if (!child) throw std::runtime_error("Cannot create Dashboard WebView2 window");
        CheckWeb(webview_set_title(child.get(), ("OGPlay Dashboard · " + std::string(instance)).c_str()));
        CheckWeb(webview_set_size(child.get(), 960, 640, WEBVIEW_HINT_MIN));
        CheckWeb(webview_set_size(child.get(), 1280, 800, WEBVIEW_HINT_NONE));
        auto* controller = static_cast<ICoreWebView2Controller*>(webview_get_native_handle(child.get(), WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
        if (!controller) throw std::runtime_error("Dashboard WebView2 controller unavailable");
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
        // No webview_bind: runtime content cannot invoke launcher operations.
        CheckWeb(webview_navigate(child.get(), url.c_str()));
        dashboards_.emplace(instance, std::move(child));
    }
    void CloseDashboard(std::string_view instance) override {
        const auto previous = window_operation_; window_operation_ = true;
        struct Reset { bool& flag; bool previous; ~Reset() { flag = previous; } } reset{window_operation_, previous};
        dashboards_.erase(std::string(instance));
    }
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
            core::JsonParseError error;
            auto document = core::JsonDocument::ParseStrict(args, error);
            const auto root = document ? document->Root() : core::JsonValue{};
            const auto arg = root.Element(0);
            const auto text = arg ? arg->String() : std::nullopt;
            if (!root.IsArray() || root.Size() != 1 || !text) throw std::invalid_argument("rpc requires one JSON string");
            const auto response = self.callbacks_.request(*text);
            CheckWeb(webview_return(self.view_, id, 0, response.c_str()));
        } catch (const std::exception& error) {
            core::JsonWriter writer;
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
                self.dashboards_.clear(); static_cast<void>(webview_terminate(self.view_)); return;
            }
            self.callbacks_.poll();
            if (self.options_.smoke_responses && self.lists_ >= *self.options_.smoke_responses && !self.smoke_capture_requested_)
                self.CaptureSmoke();
        } catch (const std::exception& error) {
            self.logger_.Write(core::LogLevel::error, "frontend.gui", "process polling failed", {}, {{"reason", std::string(error.what())}});
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
    struct DashboardDeleter { void operator()(void* window) const { if (window) webview_destroy(window); } };
    using DashboardWindow = std::unique_ptr<void, DashboardDeleter>;
    std::map<std::string, DashboardWindow> dashboards_;
    inline static NativeWebView* active_{};
    WebViewHostOptions options_;
    WebViewHostCallbacks callbacks_;
    core::Logger& logger_;
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
    WebViewHostCallbacks callbacks, core::Logger& logger) {
    if (!callbacks.request || !callbacks.poll) throw std::invalid_argument("WebView callbacks are required");
    return std::make_unique<NativeWebView>(std::move(options), std::move(callbacks), logger);
}
void OpenHostDirectory(const std::filesystem::path& path) {
    const auto result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) throw std::runtime_error("cannot open directory");
}
}  // namespace ogplay::hal
