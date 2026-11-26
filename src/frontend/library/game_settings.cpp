#include "ogplay/app/library.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace ogplay::app {
const std::vector<SettingDefinition>& GameSettingDefinitions() {
    static const auto fields = [] {
        std::vector<SettingDefinition> result;
        for (const auto& [key, group] :
             std::vector<std::pair<std::string, std::string>>{{"interpreter", "performance"},
                                                              {"max_fps", "performance"},
                                                              {"clock_mode", "performance"},
                                                              {"device_preset", "device"},
                                                              {"locale", "device"},
                                                              {"screen_resolution", "display"},
                                                              {"screen_aspect", "display"},
                                                              {"screen_width", "display"},
                                                              {"screen_height", "display"},
                                                              {"density_dpi", "display"},
                                                              {"supersample", "display"},
                                                              {"fps_overlay", "display"},
                                                              {"fps_overlay_position", "display"},
                                                              {"window_mode", "display"},
                                                              {"scaling", "display"},
                                                              {"volume", "audio"},
                                                              {"mute", "audio"},
                                                              {"keymap", "input"},
                                                              {"gamepad_deadzone", "input"},
                                                              {"network_policy", "network"},
                                                              {"tls_mode", "network"},
                                                              {"profiles_dir", "compatibility"},
                                                              {"mcp_enabled", "advanced"},
                                                              {"mcp_port", "advanced"}}) {
            auto field = FindSetting(key);
            field.group = group;
            // Per-instance values are not defaults: "Default interpreter" reads "Interpreter" here.
            constexpr std::string_view kDefault = "Default ";
            if (field.global_default && field.label.starts_with(kDefault)) {
                field.label.erase(0, kDefault.size());
                field.label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(field.label[0])));
            }
            field.global_default = false;
            result.push_back(std::move(field));
        }
        const auto pending = "Reserved · Runtime configuration is not wired yet";
        const std::vector<SettingDefinition> extra{
            {"clock_rate",
             "performance",
             "Clock rate",
             std::string("1"),
             {"0.5", "1", "1.5", "2", "3", "4"},
             0,
             0,
             pending,
             false,
             "",
             "runtimeConfig",
             false},
            {"jit_cache_mb",
             "performance",
             "JIT cache",
             std::uint32_t(64),
             {},
             1,
             4096,
             pending,
             false,
             "mb",
             "runtimeConfig",
             false},
            {"cpu_cores",
             "device",
             "Virtual CPU cores",
             std::uint32_t(1),
             {},
             1,
             32,
             "Core count the game sees; does not limit host threads",
             false,
             "",
             "",
             false},
            {"cpu_frequency_mhz",
             "device",
             "Nominal CPU frequency",
             std::uint32_t(1000),
             {},
             1,
             10000,
             "Fixed frequency the game sees; does not limit execution speed",
             false,
             "mhz",
             "",
             false},
            {"cpu_limit_parallelism",
             "performance",
             "Limit native CPU concurrency to the virtual cores",
             false,
             {},
             0,
             0,
             "Limits how many threads run ARM code at once; keeps one host thread per guest thread",
             false,
             "",
             "",
             false},
            {"cpu_max_mticks_per_second",
             "performance",
             "Native CPU rate cap",
             std::uint32_t(0),
             {},
             0,
             10000,
             "Shared by all native threads; 0 means unlimited. A tick is not a physical CPU cycle, and Java interpretation is not limited by this",
             false,
             "mticks",
             "",
             false},
            {"memory_mb",
             "device",
             "Memory",
             std::uint32_t(2048),
             {},
             128,
             65536,
             pending,
             false,
             "mb",
             "runtimeConfig",
             false},
            {"orientation",
             "display",
             "Force orientation",
             std::string("automatic"),
             {"automatic", "portrait", "landscape"},
             0,
             0,
             pending,
             false,
             "",
             "runtimeConfig",
             false},
            {"mute_unfocused",
             "audio",
             "Mute when unfocused",
             false,
             {},
             0,
             0,
             pending,
             false,
             "",
             "runtimeConfig",
             false},
            {"google_play",
             "device",
             "Google Play Store and services",
             true,
             {},
             0,
             0,
             "Simulates the Play Store with no account signed in (license checks fail; in-app purchases and Play Games sign-in are unavailable). Turning this off may prevent some games from starting.",
             false,
             "",
             "",
             false},
            {"external_guest_dir",
             "data",
             "Data pack guest mount folder",
             std::string("/sdcard"),
             {},
             0,
             0,
             "",
             false,
             "",
             "",
             false},
            {"external_dir",
             "data",
             "Data pack folder",
             std::string(),
             {},
             0,
             0,
             "leave empty to disable",
             true,
             "",
             "",
             false},
            {"data_mount_hint",
             "data",
             "Check the data pack mount after exit",
             true,
             {},
             0,
             0,
             "When the game cannot find its data, suggest a mount location after it exits",
             false,
             "",
             "",
             false},
            {"mcp_manual_step", "advanced", "MCP manual stepping", false, {}, 0, 0, "", false, "", "", false},
            {"ephemeral_sandbox",
             "advanced",
             "Temporary sandbox",
             false,
             {},
             0,
             0,
             "saves from this run are discarded on exit",
             false,
             "",
             "",
             false},
        };
        result.insert(result.end(), extra.begin(), extra.end());
        for (const auto& [key, label] :
             std::vector<std::pair<std::string, std::string>>{{"brand", "Brand"},
                                                              {"manufacturer", "Manufacturer"},
                                                              {"model", "Model"},
                                                              {"device", "Device"},
                                                              {"product", "Product"},
                                                              {"cpu_name", "CPU name"},
                                                              {"gpu_vendor", "GPU VENDOR"},
                                                              {"gpu_renderer", "GPU RENDERER"},
                                                              {"timezone", "Time zone"}})
            result.push_back({key, "device", label, std::string(), {}, 0, 0, pending, false, "", "runtimeConfig"});
        return result;
    }();
    return fields;
}

const SettingDefinition& FindGameSetting(std::string_view key) {
    const auto& fields = GameSettingDefinitions();
    const auto found = std::find_if(fields.begin(), fields.end(), [&](const auto& field) { return field.key == key; });
    if (found == fields.end()) throw std::invalid_argument("unknown instance setting: " + std::string(key));
    return *found;
}

void ValidateGameSettings(const GameSettings& settings, bool directories) {
    for (const auto& [key, value] : settings.values) {
        const auto& field = FindGameSetting(key);
        ValidateSettingValue(field, value);
        if (directories && field.directory && !std::get<std::string>(value).empty()) {
            const auto& text = std::get<std::string>(value);
            const std::filesystem::path path(
                std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
            if (!std::filesystem::is_directory(path))
                throw std::invalid_argument(field.label + ": directory unavailable");
        }
    }
}

SettingValue EffectiveGameSetting(const GameSettings& settings, const LibraryConfig& global, const LibraryEntry& entry,
                                  std::string_view key) {
    const auto& field = FindGameSetting(key);
    if (const auto it = settings.values.find(key); it != settings.values.end()) return it->second;
    if (key == "external_dir" && entry.external_dir) {
        const auto text = entry.external_dir->generic_u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }
    const auto& globals = SettingDefinitions();
    if (std::any_of(globals.begin(), globals.end(), [&](const auto& value) { return value.key == key; }))
        return Setting(global, key);
    return field.initial;
}

LibraryConfig EffectiveGameConfig(const GameSettings& settings, const LibraryConfig& global) {
    auto result = global;
    ValidateGameSettings(settings);
    for (const auto& field : SettingDefinitions())
        if (const auto it = settings.values.find(field.key); it != settings.values.end())
            SetSetting(result, field.key, it->second);
    return result;
}

LibraryEntry EffectiveGameEntry(const LibraryEntry& entry, const GameSettings& settings) {
    auto result = entry;
    ValidateGameSettings(settings);
    if (const auto it = settings.values.find("external_dir"); it != settings.values.end()) {
        const auto& text = std::get<std::string>(it->second);
        auto selected =
            text.empty()
                ? std::optional<std::filesystem::path>()
                : std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        // Any other per-game directory links; library-owned data stays unused on disk.
        if (selected != result.external_dir) result.external_owned = false;
        result.external_dir = std::move(selected);
    }
    if (const auto it = settings.values.find("external_guest_dir"); it != settings.values.end())
        result.external_guest_dir = std::get<std::string>(it->second);
    return result;
}
}  // namespace ogplay::app
