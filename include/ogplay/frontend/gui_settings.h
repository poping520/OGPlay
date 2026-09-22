#pragma once
#include "ogplay/frontend/gui_model.h"
namespace ogplay::frontend {
struct GuiSettingDefinition {
    std::string key, group, label;
    GuiSettingValue initial;
    std::vector<std::string> choices;
    std::uint32_t minimum{}, maximum{};
    std::string pending;
    bool directory{};
};
[[nodiscard]] const std::vector<GuiSettingDefinition>& GuiSettings();
[[nodiscard]] const GuiSettingDefinition& FindGuiSetting(std::string_view key);
[[nodiscard]] GuiSettingValue GuiSetting(const GuiConfig& config, std::string_view key);
void SetGuiSetting(GuiConfig& config, std::string_view key, GuiSettingValue value);
void ValidateGuiConfigValues(const GuiConfig& config);
void ValidateSettingValue(const GuiSettingDefinition& field, const GuiSettingValue& value);

// Missing keys inherit the global value (or the field default).
struct GameSettings final {
    std::map<std::string, GuiSettingValue, std::less<>> values;
    bool operator==(const GameSettings&) const = default;
};
[[nodiscard]] const std::vector<GuiSettingDefinition>& GameSettingDefinitions();
[[nodiscard]] const GuiSettingDefinition& FindGameSetting(std::string_view key);
void ValidateGameSettings(const GameSettings& settings, bool directories = false);
[[nodiscard]] GameSettings LoadGameSettings(const std::filesystem::path& entry_directory);
void SaveGameSettings(const std::filesystem::path& entry_directory, const GameSettings& settings);
[[nodiscard]] GuiSettingValue EffectiveGameSetting(const GameSettings& settings, const GuiConfig& global,
                                                   const LibraryEntry& entry, std::string_view key);
[[nodiscard]] GuiConfig EffectiveGameConfig(const GameSettings& settings, const GuiConfig& global);
[[nodiscard]] LibraryEntry EffectiveGameEntry(const LibraryEntry& entry, const GameSettings& settings);
}
