#pragma once

#include <functional>
#include <future>
#include <memory>
#include <optional>
#include "ogplay/app/apk_inspect.h"
#include "ogplay/app/archive_import.h"
#include "frontend/gui/credits.h"
#include "frontend/gui/dashboard.h"
#include "ogplay/app/agent.h"
#include "ogplay/app/ui_text.h"
#include "frontend/gui/launch.h"
#include "frontend/gui/platform.h"
#include "frontend/gui/view_model.h"

namespace ogplay::frontend {
inline bool CanDismissImportModal(bool host_dialog_pending) noexcept {
    return !host_dialog_pending;
}

using namespace ogplay::app;

// The project page the About page's Star card opens; fixed here so the page never names a URL to open.
inline constexpr std::string_view kOgplayRepositoryUrl = "https://github.com/poping520/OGPlay";

// Host operations are explicit callbacks; the RPC model has no SDL/WebView dependency.
struct GuiRpcHost final {
    std::function<LibraryViewContext(const std::vector<LibraryEntry>&)> context;
    std::function<void(const LaunchPlan&)> launch;
    std::function<void(const std::filesystem::path&)> open_directory;
    std::function<ApkImportAnalysis(const std::filesystem::path&)> analyze;
    std::function<std::future<std::optional<std::filesystem::path>>(bool)> pick;
    std::function<std::string()> timestamp;
    std::function<void()> minimize;
    // Facts keyed by stable ids; values are data (versions, file text, paths, codes), never
    // sentences, so the launcher can label them in its language. A null value is an unreadable file.
    std::function<std::map<std::string, std::optional<std::string>>()> settings_facts;
    std::function<std::map<std::string, std::string>(const LibraryEntry&, const LibraryConfig&)> game_facts;
    // Open-source acknowledgements for the About page; license texts are read only when asked for.
    std::function<std::vector<CreditProject>()> credits;
    std::function<std::vector<std::pair<std::string, std::string>>(std::string_view)> credit_notices;
    std::function<std::vector<GuiDashboard>()> dashboards;
    std::function<void(std::string_view)> open_dashboard;
    // Opens the instance's key editor window (ADR-0134) with the launch token.
    std::function<void(std::string_view)> open_key_editor;
    // OGPlay's bundled demo games: their packages, listed only while show_demo_games is on, and the
    // import of those the library lacks, run when that setting is turned back on.
    std::function<std::vector<std::string>()> demo_packages;
    std::function<void()> seed_demos;
    // RPC bindings run on the WebView UI thread; called after a successful settings save.
    std::function<void(UiLanguage)> language_changed;
    // Optional, for hosts whose picks are handles rather than host paths (Android SAF). label: what to show
    // for a picked path (a file name, /sdcard/Download/x) and the host path to guess its mount from; empty
    // keeps the path. open_tree: a picked directory handle as files to list and copy; null for a host path.
    std::function<std::string(const std::filesystem::path&)> label;
    std::function<std::unique_ptr<ImportSource>(const std::filesystem::path&)> open_tree;
    // single_game platforms: bring the running game (by installation id) back to the front, or ask it to quit.
    std::function<void(std::string_view)> resume;
    std::function<void(std::string_view)> stop;
    // Opens an http(s) page in the system browser (about.open_repository).
    std::function<void(std::string_view)> open_url;
};

class GuiRpcService final {
public:
    // platform leaves out the methods and settings of the capabilities it does not offer.
    GuiRpcService(LibraryStore& store, std::filesystem::path game_executable, GuiRpcHost host,
                  GuiPlatform platform = GuiPlatform::Desktop());
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
    GuiPlatform platform_;
    app::JsonRpcDispatcher adapter_;
};

}  // namespace ogplay::frontend
