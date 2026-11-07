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
#include "ogplay/app/logging.h"
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
    std::filesystem::path library_root;
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

    if (explicit_root.has_value()) return {*explicit_root, smoke_frames};
    return {app::DefaultLibraryRoot(), smoke_frames};
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

LibraryViewContext BuildContext(const LibraryStore& store_, const GuiProcessManager& processes_,
                                const std::vector<LibraryEntry>& entries) {
    LibraryViewContext context;
    context.running_packages = processes_.RunningPackages();
    for (const auto& entry : entries)
        if (store_.IsRunning(entry.key) && std::find(context.running_packages.begin(), context.running_packages.end(),
                                                     entry.key) == context.running_packages.end())
            context.running_packages.push_back(entry.key);
    try {
        const auto config = LoadLibraryConfig(store_.Root());
        std::map<std::optional<std::filesystem::path>, app::ProfileCatalog> catalogs;
        for (const auto& entry : entries) {
            if (!entry.metadata || entry.Damaged()) continue;
            try {
                const auto effective = EffectiveGameConfig(LoadGameSettings(entry.directory), config);
                const auto& directory = effective.profiles_dir;
                if (!catalogs.contains(directory)) {
                    catalogs.emplace(directory, directory ? app::ProfileCatalog::Load(*directory)
                                                          : app::ProfileCatalog::LoadBundled());
                }
                const auto& profiles = catalogs.at(directory);
                if (!entry.metadata->profile_id) continue;
                const auto summary = profiles.Find(*entry.metadata->profile_id);
                if (!summary) context.missing_profiles.insert(entry.key);
            } catch (const LibraryError& error) {
                if (error.Code() == LibraryErrorCode::busy) throw;
                context.profile_errors[entry.key] = error.what();
            } catch (const std::exception& error) {
                context.profile_errors[entry.key] = error.what();
            }
        }
    } catch (const LibraryError& error) {
        if (error.Code() == LibraryErrorCode::busy) throw;
        context.profile_catalog_error = error.what();
    } catch (const std::exception& error) {
        context.profile_catalog_error = error.what();
    }
    return context;
}
}  // namespace

int RunGuiCommand(int argc, const char* const argv[], app::Logger& logger) {
    const auto options = ParseOptions(argc, argv);
    try {
        PrepareLog(logger, options.library_root);
        LibraryStore store(options.library_root);
        // Bundled demos are read once; a broken one is logged and leaves the launcher without demos.
        std::vector<app::ApkImportAnalysis> demos;
        const auto seed_demos = [&] {
            for (const auto& id : SeedBundledDemos(store, demos, app::UtcTimestamp()))
                logger.Write(app::LogLevel::info, "frontend.gui.demo", "demo game added to the library", {},
                             {{"installation_id", id}});
        };
        try {
            demos = FindBundledDemos(app::BundledDataPaths().root / "demos", app::ProfileCatalog::LoadBundled());
            seed_demos();
        } catch (const std::exception& error) {
            logger.Write(app::LogLevel::warn, "frontend.gui.demo", "demo games unavailable", {},
                         {{"reason", std::string(error.what())}});
        }
        auto language = SystemUiLanguage();
        try {
            language = ResolveUiLanguage(std::get<std::string>(Setting(LoadLibraryConfig(store.Root()), "language")));
        } catch (const std::exception& error) {
            logger.Write(app::LogLevel::warn, "frontend.gui", "UI language configuration unavailable", {},
                         {{"reason", std::string(error.what())}});
        }
        GuiProcessManager processes(logger);
        std::unique_ptr<hal::WebViewHost> view;
        GuiRpcService rpc(
            store, app::ExecutablePath(),
            {[&](const auto& entries) { return BuildContext(store, processes, entries); },
             [&](const auto& plan) { processes.Launch(plan); }, [](const auto& path) { hal::OpenHostDirectory(path); },
             [&](const auto& path) {
                 const auto config = LoadLibraryConfig(store.Root());
                 const auto profiles = config.profiles_dir ? app::ProfileCatalog::Load(*config.profiles_dir)
                                                           : app::ProfileCatalog::LoadBundled();
                 return AnalyzeApkImportFile(path, profiles);
             },
             [&](bool directory) {
                 return view->PickPath(directory, Text(language, directory ? UiText::pick_data_dir : UiText::pick_apk));
             },
             [] { return app::UtcTimestamp(); }, [&] { view->Minimize(); },
             [] {
                 std::map<std::string, std::optional<std::string>> facts;
                 facts["ogplay_version"] = OGPLAY_VERSION;
                 const auto root = app::BundledDataPaths().root;
                 // Web UI licenses are part of the open-source acknowledgements (about.credits).
                 for (const auto& [key, path] : std::array<std::pair<const char*, const char*>, 1>{
                          {{"android_manifest", "android/19/manifest.json"}}}) {
                     std::error_code error;
                     const auto size = std::filesystem::file_size(root / path, error);
                     facts[key] = std::nullopt;  // missing, unreadable or over the read limit
                     if (error || size > 65536) continue;
                     std::ifstream file(root / path, std::ios::binary);
                     std::string text(static_cast<std::size_t>(size), '\0');
                     file.read(text.data(), static_cast<std::streamsize>(size));
                     if (file) facts[key] = std::move(text);
                 }
                 return facts;
             },
             [](const LibraryEntry& entry, const LibraryConfig& config) {
                 std::map<std::string, std::string> facts;
                 const auto bundled = app::BundledDataPaths();
                 const auto directory = config.profiles_dir.value_or(bundled.profiles_directory);
                 facts["profiles_dir"] = PathUtf8(directory);
                 const auto profiles = config.profiles_dir ? app::ProfileCatalog::Load(*config.profiles_dir)
                                                           : app::ProfileCatalog::LoadBundled();
                 if (entry.metadata && entry.metadata->profile_id) {
                     const auto summary = profiles.Find(*entry.metadata->profile_id);
                     facts["profile_status"] = summary ? "recorded" : "missing";
                 }
                 return facts;
             },
             [] { return LoadCredits(app::BundledDataPaths().root); },
             [](std::string_view id) { return ReadCreditNotices(app::BundledDataPaths().root, id); },
             [&] { return processes.Dashboards(); },
             [&](std::string_view instance) { view->OpenDashboard(instance, processes.DashboardPort(instance)); },
             [&](std::string_view instance) {
                 const auto editor = processes.KeyEditor(instance);
                 view->OpenKeyEditor(instance, editor.port, editor.token, KeyEditorTitle(editor, instance));
             },
             [&] {
                 std::vector<std::string> packages;
                 for (const auto& demo : demos)
                     packages.push_back(demo.manifest.package);
                 return packages;
             },
             seed_demos,
             [&](UiLanguage next) {
                 language = next;
                 view->SetShellText(ShellText(language));
             }});
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
             options.library_root / "gui-smoke.png", ShellText(language), "检查 GUI 日志和前端制品后重试。"},
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
