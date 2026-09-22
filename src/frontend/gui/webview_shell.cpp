#include "ogplay/frontend/gui.h"
#include <SDL3/SDL.h>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include "ogplay/core/logger.h"
#include "ogplay/frontend/gui_rpc.h"
#include "ogplay/frontend/gui_settings.h"
#include "ogplay/frontend/data_directory.h"
#include "ogplay/frontend/user_data_dir.h"
#include "ogplay/hal/webview_host.h"
#include "ogplay/hal/clock.h"
#include "ogplay/session/profile_apk.h"
#include "ogplay/session/quirk_registry.h"
#include "process_manager.h"

namespace ogplay::frontend {
namespace {
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
    if (argc < 2 || std::string_view(argv[1]) != "gui") {
        throw std::invalid_argument("gui command entry is invalid");
    }
    std::optional<std::filesystem::path> explicit_root;
    std::optional<std::uint64_t> smoke_frames;
    for (int index = 2; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--library-root") {
            if (explicit_root.has_value()) {
                throw std::invalid_argument("gui accepts --library-root only once");
            }
            if (++index >= argc || std::string_view(argv[index]).empty()) {
                throw std::invalid_argument("gui --library-root requires a directory");
            }
            explicit_root = std::filesystem::absolute(
                std::filesystem::path(argv[index])).lexically_normal();
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
    const auto root = UserDataDirectory();
    if (!root.has_value()) {
        throw std::runtime_error(
            "host user-data directory is unavailable; use --library-root <dir>");
    }
    return {*root, smoke_frames};
}

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void PrepareLog(core::Logger& logger, const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    const auto path = root / "gui.log";
    {
        std::ofstream truncate(path, std::ios::binary | std::ios::trunc);
        if (!truncate) {
            throw std::runtime_error("cannot create GUI log: " + PathUtf8(path));
        }
    }
    logger.AddSink(std::make_shared<core::FileSink>(path), core::LogLevel::info);
}

LibraryViewContext BuildContext(const LibraryStore& store_, const GuiProcessManager& processes_, const std::vector<LibraryEntry>& entries) {
        LibraryViewContext context;
        context.running_packages = processes_.RunningPackages();
        try {
            const auto bundled = HostBundledDataPaths();
            const auto config = LoadGuiConfig(store_.Root());
            const auto quirks = session::QuirkRegistry::LoadPackaged(bundled.quirk_registry);
            std::map<std::filesystem::path, session::TitleProfileCatalog> catalogs;
            for (const auto& entry : entries) {
                if (!entry.metadata || entry.Damaged()) continue;
                try {
                    const auto effective = EffectiveGameConfig(LoadGameSettings(entry.directory), config);
                    const auto directory = effective.profiles_dir.value_or(bundled.profiles_directory);
                    if (!catalogs.contains(directory)) catalogs.emplace(directory,
                        session::TitleProfileCatalog::LoadDirectory(directory, quirks));
                    const auto& profiles = catalogs.at(directory);
                    if (!entry.metadata->profile_id) continue;
                    const auto summary = session::FindApkProfileSummary(profiles, *entry.metadata->profile_id);
                    if (!summary) throw std::runtime_error("记录的 Profile 在所选目录中不存在。");
                    if (summary->requires_external_data) context.external_required_packages.push_back(entry.key);
                } catch (const std::exception& error) { context.profile_errors[entry.key] = error.what(); }
            }
        } catch (const std::exception& error) { context.profile_catalog_error = error.what(); }
        return context;
    }
}  // namespace
int RunGuiCommand(int argc, const char* const argv[], core::Logger& logger) {
    const auto options = ParseOptions(argc, argv);
    PrepareLog(logger, options.library_root);
    LibraryStore store(options.library_root);
    GuiProcessManager processes(logger);
    std::unique_ptr<hal::WebViewHost> view;
    GuiRpcService rpc(store, FindSiblingCliExecutable(), {
        [&](const auto& entries) { return BuildContext(store, processes, entries); },
        [&](const auto& plan) { processes.Launch(plan); },
        [](const auto& path) { hal::OpenHostDirectory(path); },
        [&](const auto& path) {
            const auto bundled = HostBundledDataPaths();
            const auto config = LoadGuiConfig(store.Root());
            const auto quirks = session::QuirkRegistry::LoadPackaged(bundled.quirk_registry);
            const auto profiles = session::TitleProfileCatalog::LoadDirectory(
                config.profiles_dir.value_or(bundled.profiles_directory), quirks);
            return AnalyzeApkImportFile(path, profiles);
        },
        [&](bool directory) { return view->PickPath(directory); },
        [] { return hal::Clock::UtcTimestamp(); },
        [&] { view->Minimize(); },
        [] {
            std::map<std::string, std::string> facts;
            facts["OGPlay 版本"] = OGPLAY_VERSION;
            const auto root = HostBundledDataPaths().root;
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
        [](const LibraryEntry& entry, const GuiConfig& config) {
            std::map<std::string, std::string> facts;
            const auto bundled = HostBundledDataPaths();
            const auto directory = config.profiles_dir.value_or(bundled.profiles_directory);
            facts["有效 Profile 目录"] = PathUtf8(directory);
            const auto quirks = session::QuirkRegistry::LoadPackaged(bundled.quirk_registry);
            const auto profiles = session::TitleProfileCatalog::LoadDirectory(directory, quirks);
            facts["Profile 状态"] = "无导入匹配记录；启动时由 CLI 重新匹配";
            if (entry.metadata && entry.metadata->profile_id) {
                const auto summary = session::FindApkProfileSummary(profiles, *entry.metadata->profile_id);
                if (!summary) throw std::runtime_error("记录的 Profile 在所选目录中不存在。");
                facts["Profile 状态"] = "目录中存在导入记录；适用性由 CLI 启动时校验";
                for (const auto& profile : profiles.Profiles()) if (profile.identity.package == *entry.metadata->profile_id) {
                    std::string names;
                    if (profile.quirks) for (const auto& name : profile.quirks->enabled) {
                        if (!names.empty()) names += ", "; names += name;
                    }
                    facts["Profile quirk 列表"] = names.empty() ? "无" : names;
                    facts["Profile 输入模板"] = profile.input ? profile.input->profile : "未指定";
                }
            }
            return facts;
        },
        [&] { return processes.Dashboards(); },
        [&](std::string_view instance) { view->OpenDashboard(instance, processes.DashboardPort(instance)); }});
    view = hal::CreateWebViewHost({
        HostBundledDataPaths().root / "webui/gui/index.html", options.smoke_frames,
        options.library_root / "gui-smoke.png"}, {
        [&](std::string_view request) {
            const auto response = rpc.Handle(request);
            core::JsonParseError error;
            auto decoded = core::JsonDocument::ParseStrict(response, error);
            if (decoded && decoded->Root().Member("error")) {
                logger.Write(core::LogLevel::error, "frontend.gui.rpc", "RPC request failed", {}, {{"response", response}});
            } else {
                auto document = core::JsonDocument::ParseStrict(request, error);
                const auto method = document ? document->Root().Member("method") : std::nullopt;
                if (method && method->String() == std::optional<std::string_view>{"library.list"}) view->RecordSmokeResponse();
            }
            return response;
        },
        [&] {
            for (const auto& result : processes.Poll()) {
                view->CloseDashboard(result.package);
                core::JsonWriter writer;
                const auto event = writer.Object();
                writer.AddString(event, "installation_id", result.package);
                writer.AddInteger(event, "exit_code", result.exit_code);
                writer.AddString(event, "log_tail", result.exit_code == 0 ? "" : ReadLogTail(result.log_path));
                view->Evaluate("window.dispatchEvent(new CustomEvent('ogplay-exit',{detail:" + writer.Serialize(event) + "}));");
            }
            for (const auto& instance : processes.TakeAutoOpen()) {
                try { view->OpenDashboard(instance, processes.DashboardPort(instance)); }
                catch (const std::exception& error) {
                    logger.Write(core::LogLevel::error, "frontend.gui.dashboard", "automatic Dashboard open failed", {}, {{"reason", std::string(error.what())}});
                    core::JsonWriter writer; const auto text = writer.String(error.what());
                    view->Evaluate("window.dispatchEvent(new CustomEvent('ogplay-dashboard-error',{detail:" + writer.Serialize(text) + "}));");
                }
            }
        }}, logger);
    return view->Run();
}
int RunGuiStandalone() {
    core::Logger logger;
    try {
        const std::array<const char*, 2> args{"ogplay", "gui"};
        return RunGuiCommand(static_cast<int>(args.size()), args.data(), logger);
    } catch (const std::exception& error) {
        static_cast<void>(SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OGPlay could not start", error.what(), nullptr));
        return 1;
    }
}
}  // namespace ogplay::frontend

