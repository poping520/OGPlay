#pragma once

#include <functional>
#include <future>
#include <memory>
#include "ogplay/app/apk_inspect.h"
#include "frontend/gui/dashboard.h"
#include "ogplay/app/agent.h"
#include "frontend/gui/launch.h"
#include "frontend/gui/view_model.h"

namespace ogplay::frontend {
inline bool CanDismissImportModal(bool host_dialog_pending) noexcept { return !host_dialog_pending; }
using namespace ogplay::app;

// Host operations are explicit callbacks; the RPC model has no SDL/WebView dependency.
struct GuiRpcHost final {
    std::function<LibraryViewContext(const std::vector<LibraryEntry>&)> context;
    std::function<void(const LaunchPlan&)> launch;
    std::function<void(const std::filesystem::path&)> open_directory;
    std::function<ApkImportAnalysis(const std::filesystem::path&)> analyze;
    std::function<std::future<std::optional<std::filesystem::path>>(bool)> pick;
    std::function<std::string()> timestamp;
    std::function<void()> minimize;
    std::function<std::map<std::string, std::string>()> settings_facts;
    std::function<std::map<std::string, std::string>(const LibraryEntry&, const LibraryConfig&)> game_facts;
    std::function<std::vector<GuiDashboard>()> dashboards;
    std::function<void(std::string_view)> open_dashboard;
    // Opens the instance's key editor window (ADR-0134) with the launch token.
    std::function<void(std::string_view)> open_key_editor;
};

class GuiRpcService final {
public:
    GuiRpcService(LibraryStore& store, std::filesystem::path game_executable, GuiRpcHost host);
    GuiRpcService(const GuiRpcService&) = delete;
    GuiRpcService& operator=(const GuiRpcService&) = delete;
    GuiRpcService(GuiRpcService&&) = delete;
    GuiRpcService& operator=(GuiRpcService&&) = delete;
    [[nodiscard]] std::string Handle(std::string_view request);
private:
    [[nodiscard]] app::ConsoleResponse Request(std::string_view method, app::JsonValue params);
    struct ImportState;
    bool ImportBusy() const;
    bool SettingsBusy() const;
    [[nodiscard]] app::ConsoleResponse SettingsRequest(std::string_view method, app::JsonValue params);
    [[nodiscard]] app::ConsoleResponse GameSettingsRequest(std::string_view method, app::JsonValue params);
    [[nodiscard]] app::ConsoleResponse ImportRequest(std::string_view method, app::JsonValue params);
    [[nodiscard]] app::ConsoleResponse DataHintRequest(std::string_view method, app::JsonValue params);
    std::shared_ptr<ImportState> import_;
    LibraryStore& store_;
    std::filesystem::path game_executable_;
    GuiRpcHost host_;
    app::JsonRpcDispatcher adapter_;
};

}  // namespace ogplay::frontend
