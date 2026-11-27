#include "ogplay/app/library.h"
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace ogplay::app {
const std::vector<SettingDefinition>& GameSettingDefinitions() {
    static const auto fields = [] {
        // Settings of this game alone, with no global value: each goes before the global field it names (an empty
        // name: at the end), in that field's group.
        const std::vector<std::pair<std::string_view, SettingDefinition>> instance{
            {"data_mount_hint", SettingDefinition{"external_dir",
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
                                                  false}},
            {"data_mount_hint", SettingDefinition{"external_guest_dir",
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
                                                  false}},
            {"profiles_dir", SettingDefinition{"ephemeral_sandbox",
                                               "data",
                                               "Temporary sandbox",
                                               false,
                                               {},
                                               0,
                                               0,
                                               "saves from this run are discarded on exit",
                                               false,
                                               "",
                                               "",
                                               false}},
            {"",
             SettingDefinition{
                 "mcp_manual_step", "developer", "MCP manual stepping", false, {}, 0, 0, "", false, "", "", false}},
        };
        // Every global field a game can override, in the global order and group.
        std::vector<SettingDefinition> result;
        for (const auto& field : SettingDefinitions()) {
            for (const auto& [before, own] : instance)
                if (before == field.key) result.push_back(own);
            if (!field.global_only) result.push_back(field);
        }
        for (const auto& [before, own] : instance)
            if (before.empty()) result.push_back(own);
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
