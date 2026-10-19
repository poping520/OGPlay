#include "ogplay/app/library.h"
#include <algorithm>
namespace ogplay::app {
namespace {
void RequireDirectory(const std::filesystem::path& path, std::string_view name) {
    if (!std::filesystem::is_directory(path)) throw LibraryError(LibraryErrorCode::not_found, std::string(name) + " is unavailable", path);
}
}
std::filesystem::path LauncherSandboxRoot(
    const std::filesystem::path& library_root) {
    if (library_root.empty()) {
        throw LibraryError(LibraryErrorCode::invalid_argument,
                            "library root must not be empty");
    }
    return (std::filesystem::absolute(library_root).lexically_normal() /
            "sandbox").lexically_normal();
}

ResolvedLaunch ResolveLaunch(const LibraryStore& store, std::string_view id, LaunchMode mode) {
    const auto entries = store.LoadEntries();
    const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.key == id; });
    if (entry == entries.end()) throw LibraryError(LibraryErrorCode::not_found, "library entry does not exist");
    return ResolveLaunch(store, *entry, LoadLibraryConfig(store.Root()), mode);
}
ResolvedLaunch ResolveLaunch(const LibraryStore& store, const LibraryEntry& source_entry, const LibraryConfig& global, LaunchMode mode) {
    if (source_entry.directory.lexically_normal() != (store.EntriesRoot() / source_entry.key).lexically_normal())
        throw LibraryError(LibraryErrorCode::invalid_argument, "entry is outside library");
    if (mode != LaunchMode::normal && mode != LaunchMode::preflight && mode != LaunchMode::diagnostic) throw std::invalid_argument("unknown launch mode");
    const auto settings = LoadGameSettings(source_entry.directory);
    const auto config = EffectiveGameConfig(settings, global);
    const auto entry = EffectiveGameEntry(source_entry, settings);
    ValidateLibraryConfigValues(config);
    if (entry.Damaged() || !entry.metadata) throw LibraryError(LibraryErrorCode::corrupt_config, "damaged library entry cannot be launched", entry.directory);
    if (config.profiles_dir) RequireDirectory(*config.profiles_dir, "Profile directory");
    if (entry.metadata->external_dir) RequireDirectory(*entry.metadata->external_dir, "external data directory");
    ResolvedLaunch result;
    auto& o = result.options;
    o.apk_path = std::filesystem::absolute(entry.directory / "game.apk").lexically_normal();
    if (!std::filesystem::is_regular_file(o.apk_path)) throw LibraryError(LibraryErrorCode::not_found, "library APK copy is missing", o.apk_path);
    o.profiles_directory = config.profiles_dir;
    o.external_directory = entry.metadata->external_dir;
    if (o.external_directory) o.external_guest_directory = entry.metadata->external_guest_dir;
    o.surface_width = std::get<std::uint32_t>(EffectiveGameSetting(settings, global, entry, "surface_width"));
    o.surface_height = std::get<std::uint32_t>(EffectiveGameSetting(settings, global, entry, "surface_height"));
    o.supersample_factor = std::get<std::uint32_t>(EffectiveGameSetting(settings, global, entry, "supersample"));
    o.cpu_config.cores = std::get<std::uint32_t>(EffectiveGameSetting(settings, global, entry, "cpu_cores"));
    o.cpu_config.frequency_mhz = std::get<std::uint32_t>(EffectiveGameSetting(settings, global, entry, "cpu_frequency_mhz"));
    o.cpu_config.execution.limit_parallelism = std::get<bool>(EffectiveGameSetting(settings, global, entry, "cpu_limit_parallelism"));
    o.cpu_config.execution.max_mticks_per_second = std::get<std::uint32_t>(EffectiveGameSetting(settings, global, entry, "cpu_max_mticks_per_second"));
    const auto interpreter = std::get<std::string>(Setting(config, "interpreter"));
    if (interpreter != "default") o.dexvm_interpreter = interpreter == "threaded" ? InterpreterBackend::threaded : InterpreterBackend::switch_dispatch;
    const bool mcp = std::get<bool>(Setting(config, "mcp_enabled"));
    const bool manual = std::get<bool>(EffectiveGameSetting(settings, global, entry, "mcp_manual_step"));
    if (mode != LaunchMode::preflight) {
        if (manual && !mcp) throw std::invalid_argument("MCP 手动步进需要启用 MCP。");
        if (mcp) {
            o.mcp_port = static_cast<std::uint16_t>(std::get<std::uint32_t>(Setting(config, "mcp_port")));
            result.mcp_port = o.mcp_port;
            result.dashboard_auto_open = std::get<bool>(Setting(config, "dashboard_auto_open"));
        }
        o.mcp_manual_step = manual;
    }
    o.preflight = mode == LaunchMode::preflight;
    o.diagnostics_enabled = mode == LaunchMode::diagnostic;
    o.ephemeral_sandbox = std::get<bool>(EffectiveGameSetting(settings, global, entry, "ephemeral_sandbox"));
    if (!o.ephemeral_sandbox) o.sandbox_directory = LauncherSandboxRoot(store.Root());
    o.installation_id = entry.key;
    o.library_root = store.Root();
    o.input_config_path = std::filesystem::absolute(entry.directory / "input.toml").lexically_normal();
    ValidateGameRunOptions(o);
    return result;
}
}
