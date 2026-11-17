#include "frontend/gui/shell.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "ogplay/app/logging.h"
#include "frontend/gui/launcher_services.h"
#include "frontend/gui/rpc.h"
#include "ogplay/app/library.h"
#include "ogplay/app/demo_games.h"
#include "ogplay/app/host.h"
#include "ogplay/hal/webview_host.h"
#include "ogplay/app/profiles.h"
#include "process_manager.h"

namespace ogplay::frontend {
using namespace ogplay::app;

namespace {
std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

hal::WebViewShellText ShellText(UiLanguage language) {
    return {
        .menu_hide_app = std::string(Text(language, UiText::menu_hide_app)),
        .menu_hide_others = std::string(Text(language, UiText::menu_hide_others)),
        .menu_show_all = std::string(Text(language, UiText::menu_show_all)),
        .menu_quit = std::string(Text(language, UiText::menu_quit)),
        .menu_edit = std::string(Text(language, UiText::menu_edit)),
        .menu_undo = std::string(Text(language, UiText::menu_undo)),
        .menu_redo = std::string(Text(language, UiText::menu_redo)),
        .menu_cut = std::string(Text(language, UiText::menu_cut)),
        .menu_copy = std::string(Text(language, UiText::menu_copy)),
        .menu_paste = std::string(Text(language, UiText::menu_paste)),
        .menu_select_all = std::string(Text(language, UiText::menu_select_all)),
        .menu_window = std::string(Text(language, UiText::menu_window)),
        .menu_minimize = std::string(Text(language, UiText::menu_minimize)),
        .menu_close = std::string(Text(language, UiText::menu_close)),
        .tray_show = std::string(Text(language, UiText::tray_show)),
        .tray_quit = std::string(Text(language, UiText::tray_quit)),
    };
}

// The page shows the game's language, so its window does too, whatever the launcher has switched to since.
std::string KeyEditorTitle(const GuiProcessManager::KeyEditorAddress& editor, std::string_view instance) {
    return std::string(Text(editor.language, UiText::key_editor_window_title)) + " · " + std::string(instance);
}

struct GuiOptions final {
    std::optional<std::filesystem::path> library_root;
    std::optional<std::uint64_t> smoke_frames;
};

[[nodiscard]] std::uint64_t ParsePositiveCount(const std::string_view text, const std::string_view option) {
    std::uint64_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0) {
        throw std::invalid_argument(std::string(option) + " requires a positive integer");
    }
    return value;
}

[[nodiscard]] GuiOptions ParseOptions(const int argc, const char* const argv[]) {
    std::optional<std::filesystem::path> explicit_root;
    std::optional<std::uint64_t> smoke_frames;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--library-root") {
            if (explicit_root.has_value()) {
                throw std::invalid_argument("gui accepts --library-root only once");
            }
            if (++index >= argc || std::string_view(argv[index]).empty()) {
                throw std::invalid_argument("gui --library-root requires a directory");
            }
            explicit_root = std::filesystem::absolute(Utf8Path(argv[index])).lexically_normal();
        } else if (option == "--smoke-frames") {
            if (smoke_frames.has_value()) {
                throw std::invalid_argument("gui accepts --smoke-frames only once");
            }
            if (++index >= argc) {
                throw std::invalid_argument("gui --smoke-frames requires a count");
            }
            smoke_frames = ParsePositiveCount(argv[index], "gui --smoke-frames");
        } else {
            throw std::invalid_argument("unknown or incomplete gui option: " + std::string(option));
        }
    }

    return {explicit_root, smoke_frames};
}

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void PrepareLog(app::Logger& logger, const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    const auto path = root / "gui.log";
    {
        std::ofstream truncate(path, std::ios::binary | std::ios::trunc);
        if (!truncate) {
            throw std::runtime_error("cannot create GUI log: " + PathUtf8(path));
        }
    }
    logger.SetFile(path);
}

}  // namespace

int RunGuiCommand(int argc, const char* const argv[], app::Logger& logger) {
    const auto options = ParseOptions(argc, argv);
    try {
        const auto location = options.library_root
                                  ? app::LibraryRootLocation{*options.library_root, false, std::nullopt}
                                  : app::DefaultLibraryLocation();
        PrepareLog(logger, location.root);
        std::vector<app::LogField> fields{
            {"mode", std::string(location.portable ? "portable" : "standard")},
            {"root", PathUtf8(location.root)},
        };
        if (location.legacy_root) fields.push_back({"legacy_root", PathUtf8(*location.legacy_root)});
        logger.Write(app::LogLevel::info, "frontend.user_data", "selected launcher data directory", {},
                     std::move(fields));
        LibraryStore store(location.root);
        LauncherServices services(store, logger);
        auto language = services.Language();
        GuiProcessManager processes(logger);
        std::unique_ptr<hal::WebViewHost> view;
        auto host = services.Host([&] { return processes.RunningPackages(); });
        host.launch = [&](const auto& plan) { processes.Launch(plan); };
        host.open_directory = [](const auto& path) { hal::OpenHostDirectory(path); };
        host.open_url = [](std::string_view url) {
            if (!SDL_OpenURL(std::string(url).c_str()))
                throw std::runtime_error(std::string("SDL_OpenURL failed: ") + SDL_GetError());
        };
        host.pick = [&](bool directory) {
            return view->PickPath(directory, Text(language, directory ? UiText::pick_data_dir : UiText::pick_apk));
        };
        host.minimize = [&] { view->Minimize(); };
        host.dashboards = [&] { return processes.Dashboards(); };
        host.open_dashboard = [&](std::string_view instance) {
            view->OpenDashboard(instance, processes.DashboardPort(instance));
        };
        host.open_key_editor = [&](std::string_view instance) {
            const auto editor = processes.KeyEditor(instance);
            view->OpenKeyEditor(instance, editor.port, editor.token, KeyEditorTitle(editor, instance));
        };
        host.language_changed = [&](UiLanguage next) {
            language = next;
            view->SetShellText(ShellText(language));
        };
        GuiRpcService rpc(store, app::ExecutablePath(), std::move(host));
        // WebView teardown drains callbacks the page already queued: destroy it while rpc is
        // still alive, and refuse those late callbacks because view is already null by then.
        bool closing{};

        struct ViewCloser {
            bool& closing;
            std::unique_ptr<hal::WebViewHost>& view;

            ~ViewCloser() {
                closing = true;
                view.reset();
            }
        } closer{closing, view};

        view = hal::CreateWebViewHost(
            {app::BundledDataPaths().root / "webui/gui/index.html", options.smoke_frames,
             location.root / "gui-smoke.png", ShellText(language), "检查 GUI 日志和前端制品后重试。", location.root},
            {[&](std::string_view request) {
                 if (closing) {
                     app::JsonWriter writer;
                     const auto root = writer.Object(), error = writer.Object();
                     writer.AddString(root, "jsonrpc", "2.0");
                     writer.AddNull(root, "id");
                     writer.AddInteger(error, "code", -32002);
                     writer.AddString(error, "message", "launcher is closing");
                     writer.AddString(error, "next_step", "重新打开 OGPlay。");
                     writer.Add(root, "error", error);
                     return writer.Serialize(root);
                 }
                 const auto response = rpc.Handle(request);
                 app::JsonParseError error;
                 auto decoded =
                     app::JsonDocument::ParseStrict(response, error, app::JsonRpcDispatcher::kMaximumResponseBytes);
                 if (decoded && decoded->Root().Member("error")) {
                     logger.Write(app::LogLevel::error, "frontend.gui.rpc", "RPC request failed", {},
                                  {{"response", response}});
                 } else {
                     auto document = app::JsonDocument::ParseStrict(request, error);
                     const auto method = document ? document->Root().Member("method") : std::nullopt;
                     if (method && method->String() == std::optional<std::string_view>{"library.list"})
                         view->RecordSmokeResponse();
                 }
                 return response;
             },
             [&] {
                 if (closing) return;
                 for (const auto& result : processes.Poll()) {
                     view->CloseDashboard(result.package);
                     view->CloseKeyEditor(result.package);
                     app::JsonWriter writer;
                     const auto event = writer.Object();
                     writer.AddString(event, "installation_id", result.package);
                     writer.AddInteger(event, "exit_code", result.exit_code);
                     writer.AddString(event, "log_tail", result.exit_code == 0 ? "" : ReadLogTail(result.log_path));
                     view->Evaluate("window.dispatchEvent(new CustomEvent('ogplay-exit',{detail:" +
                                    writer.Serialize(event) + "}));");
                 }
                 // In-game F2 (ADR-0134): the game routes the request here while this launcher polls it.
                 for (const auto& instance : processes.TakeKeyEditorRequests()) {
                     try {
                         const auto editor = processes.KeyEditor(instance);
                         view->OpenKeyEditor(instance, editor.port, editor.token, KeyEditorTitle(editor, instance));
                     } catch (const std::exception& error) {
                         logger.Write(app::LogLevel::error, "frontend.gui.key_editor", "key editor open failed", {},
                                      {{"reason", std::string(error.what())}});
                     }
                 }
                 // The editor page's 完成 (control.resume {close:true}) closes its window here.
                 for (const auto& instance : processes.TakeKeyEditorCloses())
                     view->CloseKeyEditor(instance);
                 for (const auto& instance : processes.TakeAutoOpen()) {
                     try {
                         view->OpenDashboard(instance, processes.DashboardPort(instance));
                     } catch (const std::exception& error) {
                         logger.Write(app::LogLevel::error, "frontend.gui.dashboard", "automatic Dashboard open failed",
                                      {}, {{"reason", std::string(error.what())}});
                         app::JsonWriter writer;
                         const auto text = writer.String(error.what());
                         view->Evaluate("window.dispatchEvent(new CustomEvent('ogplay-dashboard-error',{detail:" +
                                        writer.Serialize(text) + "}));");
                     }
                 }
             }},
            logger);
        return view->Run();
    } catch (const std::exception& error) {
        if (!options.smoke_frames) {
            static_cast<void>(
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OGPlay could not start", error.what(), nullptr));
        }
        throw;
    }
}
}  // namespace ogplay::frontend
