#pragma once

#include <functional>
#include <future>
#include <memory>
#include "ogplay/frontend/gui_import.h"
#include "ogplay/agent/json_rpc.h"
#include "ogplay/frontend/gui_launch.h"
#include "ogplay/frontend/gui_view_model.h"

namespace ogplay::frontend {

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
};

class GuiRpcService final {
public:
    GuiRpcService(LibraryStore& store, std::filesystem::path cli, GuiRpcHost host);
    GuiRpcService(const GuiRpcService&) = delete;
    GuiRpcService& operator=(const GuiRpcService&) = delete;
    GuiRpcService(GuiRpcService&&) = delete;
    GuiRpcService& operator=(GuiRpcService&&) = delete;
    [[nodiscard]] std::string Handle(std::string_view request);
private:
    [[nodiscard]] agent::ControlResponse Request(std::string_view method, core::JsonValue params);
    struct ImportState;
    bool ImportBusy() const;
    bool SettingsBusy() const;
    [[nodiscard]] agent::ControlResponse SettingsRequest(std::string_view method, core::JsonValue params);
    [[nodiscard]] agent::ControlResponse ImportRequest(std::string_view method, core::JsonValue params);
    std::shared_ptr<ImportState> import_;
    LibraryStore& store_;
    std::filesystem::path cli_;
    GuiRpcHost host_;
    agent::JsonRpcAdapter adapter_;
};

}  // namespace ogplay::frontend
