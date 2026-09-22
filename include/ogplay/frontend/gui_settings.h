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
}
