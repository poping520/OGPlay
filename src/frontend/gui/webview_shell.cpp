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
#include "ogplay/app/host.h"
#include "ogplay/hal/webview_host.h"
#include "ogplay/app/profiles.h"
#include "process_manager.h"

namespace ogplay::frontend {
using namespace ogplay::app;
namespace {
std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string_view(
        reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
struct GuiOptions final {
    std::filesystem::path library_root;
    std::optional<std::uint64_t> smoke_frames;
};

[[nodiscard]] std::uint64_t ParsePositiveCount(const std::string_view text,
                                                const std::string_view option) {
    std::uint64_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value == 0) {
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
            explicit_root = std::filesystem::absolute(
                Utf8Path(argv[index])).lexically_normal();
        } else if (option == "--smoke-frames") {
            if (smoke_frames.has_value()) {
                throw std::invalid_argument("gui accepts --smoke-frames only once");
            }
            if (++index >= argc) {
                throw std::invalid_argument("gui --smoke-frames requires a count");
            }
            smoke_frames = ParsePositiveCount(argv[index], "gui --smoke-frames");
        } else {
            throw std::invalid_argument("unknown or incomplete gui option: " +
                                        std::string(option));
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

LibraryViewContext BuildContext(const LibraryStore& store_, const GuiProcessManager& processes_, const std::vector<LibraryEntry>& entries) {
        LibraryViewContext context;
        context.running_packages = processes_.RunningPackages();
        for (const auto& entry : entries) if (store_.IsRunning(entry.key) &&
            std::find(context.running_packages.begin(), context.running_packages.end(), entry.key) == context.running_packages.end())
            context.running_packages.push_back(entry.key);
        try {
            const auto bundled = app::BundledDataPaths();
            const auto config = LoadLibraryConfig(store_.Root());
            std::map<std::filesystem::path, app::ProfileCatalog> catalogs;
            for (const auto& entry : entries) {
                if (!entry.metadata || entry.Damaged()) continue;
                try {
                    const auto effective = EffectiveGameConfig(LoadGameSettings(entry.directory), config);
                    const auto directory = effective.profiles_dir.value_or(bundled.profiles_directory);
                    if (!catalogs.contains(directory)) catalogs.emplace(directory,
                        app::ProfileCatalog::Load(directory));
                    const auto& profiles = catalogs.at(directory);
                    if (!entry.metadata->profile_id) continue;
                    const auto summary = profiles.Find(*entry.metadata->profile_id);
                    if (!summary) throw std::runtime_error("记录的 Profile 在所选目录中不存在。");
                } catch (const LibraryError& error) {
                    if (error.Code() == LibraryErrorCode::busy) throw;
                    context.profile_errors[entry.key] = error.what();
                } catch (const std::exception& error) { context.profile_errors[entry.key] = error.what(); }
            }
        } catch (const LibraryError& error) {
            if (error.Code() == LibraryErrorCode::busy) throw;
            context.profile_catalog_error = error.what();
        } catch (const std::exception& error) { context.profile_catalog_error = error.what(); }
        return context;
    }
}  // namespace
int RunGuiCommand(int argc, const char* const argv[], app::Logger& logger) {
    const auto options = ParseOptions(argc, argv);
    try {
        PrepareLog(logger, options.library_root);
        LibraryStore store(options.library_root);
        GuiProcessManager processes(logger);
        std::unique_ptr<hal::WebViewHost> view;
        GuiRpcService rpc(store, app::ExecutablePath(), {
            [&](const auto& entries) { return BuildContext(store, processes, entries); },
            [&](const auto& plan) { processes.Launch(plan); },
            [](const auto& path) { hal::OpenHostDirectory(path); },
            [&](const auto& path) {
                const auto bundled = app::BundledDataPaths();
                const auto config = LoadLibraryConfig(store.Root());
                const auto profiles = app::ProfileCatalog::Load(
                    config.profiles_dir.value_or(bundled.profiles_directory));
                return AnalyzeApkImportFile(path, profiles);
            },
            [&](bool directory) { return view->PickPath(directory); },
            [] { return app::UtcTimestamp(); },
            [&] { view->Minimize(); },
            [] {
                std::map<std::string, std::string> facts;
                facts["OGPlay 版本"] = OGPLAY_VERSION;
                const auto root = app::BundledDataPaths().root;
                for (const auto& [key, path] : std::array<std::pair<const char*, const char*>, 3>{{
                    {"Web UI 构建清单", "webui/gui/manifest.json"},
                    {"Android / BootDex / guest JNI 制品清单", "android/19/manifest.json"},
                    {"Web UI 第三方许可", "webui/gui/THIRD-PARTY-LICENSES.txt"}}}) {
                    std::error_code error;
                    const auto size = std::filesystem::file_size(root / path, error);
                    if (error || size > 65536) { facts[key] = "文件不可用或超过读取上限"; continue; }
                    std::ifstream file(root / path, std::ios::binary);
                    std::string text(static_cast<std::size_t>(size), '\0');
                    file.read(text.data(), static_cast<std::streamsize>(size));
                    facts[key] = file ? text : "读取失败";
                }
                return facts;
            },
            [](const LibraryEntry& entry, const LibraryConfig& config) {
                std::map<std::string, std::string> facts;
                const auto bundled = app::BundledDataPaths();
                const auto directory = config.profiles_dir.value_or(bundled.profiles_directory);
                facts["有效 Profile 目录"] = PathUtf8(directory);
                const auto profiles = app::ProfileCatalog::Load(directory);
                facts["Profile 状态"] = "无导入匹配记录；启动时由运行服务重新匹配";
                if (entry.metadata && entry.metadata->profile_id) {
                    const auto summary = profiles.Find(*entry.metadata->profile_id);
                    if (!summary) throw std::runtime_error("记录的 Profile 在所选目录中不存在。");
                    facts["Profile 状态"] = "目录中存在导入记录；适用性由运行服务启动时校验";
                    std::string names;
                    for (const auto& name : summary->quirks) { if (!names.empty()) names += ", "; names += name; }
                    facts["Profile quirk 列表"] = names.empty() ? "无" : names;
                }
                return facts;
            },
            [&] { return processes.Dashboards(); },
            [&](std::string_view instance) { view->OpenDashboard(instance, processes.DashboardPort(instance)); },
            [&](std::string_view instance) {
                const auto editor = processes.KeyEditor(instance);
                view->OpenKeyEditor(instance, editor.port, editor.token);
            }});
        view = hal::CreateWebViewHost({
            app::BundledDataPaths().root / "webui/gui/index.html", options.smoke_frames,
            options.library_root / "gui-smoke.png"}, {
            [&](std::string_view request) {
                const auto response = rpc.Handle(request);
                app::JsonParseError error;
                auto decoded = app::JsonDocument::ParseStrict(response, error);
                if (decoded && decoded->Root().Member("error")) {
                    logger.Write(app::LogLevel::error, "frontend.gui.rpc", "RPC request failed", {}, {{"response", response}});
                } else {
                    auto document = app::JsonDocument::ParseStrict(request, error);
                    const auto method = document ? document->Root().Member("method") : std::nullopt;
                    if (method && method->String() == std::optional<std::string_view>{"library.list"}) view->RecordSmokeResponse();
                }
                return response;
            },
            [&] {
                for (const auto& result : processes.Poll()) {
                    view->CloseDashboard(result.package);
                    view->CloseKeyEditor(result.package);
                    app::JsonWriter writer;
                    const auto event = writer.Object();
                    writer.AddString(event, "installation_id", result.package);
                    writer.AddInteger(event, "exit_code", result.exit_code);
                    writer.AddString(event, "log_tail", result.exit_code == 0 ? "" : ReadLogTail(result.log_path));
                    view->Evaluate("window.dispatchEvent(new CustomEvent('ogplay-exit',{detail:" + writer.Serialize(event) + "}));");
                }
                // In-game F2 (ADR-0134): the game routes the request here while this launcher polls it.
                for (const auto& instance : processes.TakeKeyEditorRequests()) {
                    try {
                        const auto editor = processes.KeyEditor(instance);
                        view->OpenKeyEditor(instance, editor.port, editor.token);
                    } catch (const std::exception& error) {
                        logger.Write(app::LogLevel::error, "frontend.gui.key_editor", "key editor open failed", {}, {{"reason", std::string(error.what())}});
                    }
                }
                for (const auto& instance : processes.TakeAutoOpen()) {
                    try { view->OpenDashboard(instance, processes.DashboardPort(instance)); }
                    catch (const std::exception& error) {
                        logger.Write(app::LogLevel::error, "frontend.gui.dashboard", "automatic Dashboard open failed", {}, {{"reason", std::string(error.what())}});
                        app::JsonWriter writer; const auto text = writer.String(error.what());
                        view->Evaluate("window.dispatchEvent(new CustomEvent('ogplay-dashboard-error',{detail:" + writer.Serialize(text) + "}));");
                    }
                }
            }}, logger);
        return view->Run();
    } catch (const std::exception& error) {
        if (!options.smoke_frames) {
            static_cast<void>(SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OGPlay could not start", error.what(), nullptr));
        }
        throw;
    }
}
}  // namespace ogplay::frontend
