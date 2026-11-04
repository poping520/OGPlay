#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace ogplay::app {
class Logger;
}

namespace ogplay::hal {

struct WebViewShellText final {
    std::string menu_hide_app;
    std::string menu_hide_others;
    std::string menu_show_all;
    std::string menu_quit;
    std::string menu_edit;
    std::string menu_undo;
    std::string menu_redo;
    std::string menu_cut;
    std::string menu_copy;
    std::string menu_paste;
    std::string menu_select_all;
    std::string menu_window;
    std::string menu_minimize;
    std::string menu_close;
    std::string tray_show;
    std::string tray_quit;
};

struct WebViewHostOptions final {
    std::filesystem::path document;
    std::optional<std::uint64_t> smoke_responses;
    std::filesystem::path smoke_image;
    WebViewShellText shell_text;
    std::string rpc_failure_next_step;
};

struct WebViewHostCallbacks final {
    std::function<std::string(std::string_view)> request;
    std::function<void()> poll;
};

// Main-thread-only host. Native handles never cross this interface.
class WebViewHost {
public:
    virtual ~WebViewHost() = default;
    virtual int Run() = 0;
    virtual void Evaluate(std::string_view script) = 0;
    virtual void RecordSmokeResponse() = 0;
    virtual void Minimize() = 0;
    virtual void SetShellText(const WebViewShellText&) = 0;
    // Independent loopback window; never binds the launcher's rpc bridge.
    virtual void OpenDashboard(std::string_view instance, std::uint16_t port) = 0;
    virtual void CloseDashboard(std::string_view instance) = 0;
    // Key editor window (ADR-0134) on the game's loopback service; the token is handed to the
    // page through session storage, never in the URL.
    virtual void OpenKeyEditor(std::string_view instance, std::uint16_t port, std::string_view token,
                               std::string_view title) = 0;
    virtual void CloseKeyEditor(std::string_view instance) = 0;
    virtual std::future<std::optional<std::filesystem::path>> PickPath(bool directory, std::string_view title) = 0;
};

[[nodiscard]] std::unique_ptr<WebViewHost> CreateWebViewHost(WebViewHostOptions options, WebViewHostCallbacks callbacks,
                                                             app::Logger& logger);
void OpenHostDirectory(const std::filesystem::path& path);

}  // namespace ogplay::hal
