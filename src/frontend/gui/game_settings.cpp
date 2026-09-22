#include "ogplay/frontend/gui_settings.h"
#include <algorithm>
#include <stdexcept>

namespace ogplay::frontend {
const std::vector<GuiSettingDefinition>& GameSettingDefinitions() {
    static const auto fields = [] {
        std::vector<GuiSettingDefinition> result;
        for (const auto& [key, group] : std::vector<std::pair<std::string, std::string>>{
            {"interpreter", "performance"}, {"clock_mode", "performance"}, {"device_preset", "device"},
            {"supersample", "display"}, {"window_mode", "display"}, {"scaling", "display"},
            {"volume", "audio"}, {"mute", "audio"}, {"input_template", "input"}, {"keymap", "input"},
            {"gamepad_deadzone", "input"}, {"network_policy", "network"}, {"profiles_dir", "compatibility"},
            {"mcp_enabled", "advanced"}, {"mcp_port", "advanced"}}) {
            auto field = FindGuiSetting(key); field.group = group; result.push_back(std::move(field));
        }
        const auto pending = "预留 · 运行时配置入口尚未接入";
        const std::vector<GuiSettingDefinition> extra{
            {"max_fps", "performance", "最大帧率", std::string("unlimited"), {"30", "60", "120", "unlimited"}, 0, 0, pending},
            {"clock_rate", "performance", "时钟倍率", std::string("1"), {"0.5", "1", "1.5", "2", "3", "4"}, 0, 0, pending},
            {"jit_cache_mb", "performance", "JIT 缓存（MiB）", std::uint32_t(64), {}, 1, 4096, pending},
            {"cpu_cores", "device", "CPU 核数", std::uint32_t(4), {}, 1, 64, pending},
            {"screen_width", "device", "屏幕宽度", std::uint32_t(1280), {}, 1, 16384, pending},
            {"screen_height", "device", "屏幕高度", std::uint32_t(720), {}, 1, 16384, pending},
            {"density_dpi", "device", "屏幕密度（DPI）", std::uint32_t(320), {}, 72, 1280, pending},
            {"memory_mb", "device", "内存（MiB）", std::uint32_t(2048), {}, 128, 65536, pending},
            {"orientation", "display", "强制方向", std::string("automatic"), {"automatic", "portrait", "landscape"}, 0, 0, pending},
            {"fps_overlay", "display", "FPS 叠层", false, {}, 0, 0, pending},
            {"mute_unfocused", "audio", "失焦静音", false, {}, 0, 0, pending},
            {"external_dir", "data", "数据包目录（留空明确禁用）", std::string(), {}, 0, 0, "", true},
            {"mcp_manual_step", "advanced", "MCP 手动步进", false},
            {"ephemeral_sandbox", "advanced", "临时沙盒（退出不保留本次存档）", false},
        };
        result.insert(result.end(), extra.begin(), extra.end());
        for (const auto& [key, label] : std::vector<std::pair<std::string, std::string>>{
            {"brand", "品牌"}, {"manufacturer", "厂商"}, {"model", "型号"}, {"device", "设备"},
            {"product", "产品"}, {"cpu_name", "CPU 名称"}, {"gpu_vendor", "GPU VENDOR"},
            {"gpu_renderer", "GPU RENDERER"}, {"locale", "语言/地区"}, {"timezone", "时区"}})
            result.push_back({key, "device", label, std::string(), {}, 0, 0, pending});
        return result;
    }();
    return fields;
}
const GuiSettingDefinition& FindGameSetting(std::string_view key) {
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
            const std::filesystem::path path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
            if (!std::filesystem::is_directory(path)) throw std::invalid_argument(field.label + ": directory unavailable");
        }
    }
}
GuiSettingValue EffectiveGameSetting(const GameSettings& settings, const GuiConfig& global,
                                    const LibraryEntry& entry, std::string_view key) {
    const auto& field = FindGameSetting(key);
    if (const auto it = settings.values.find(key); it != settings.values.end()) return it->second;
    if (key == "external_dir" && entry.metadata && entry.metadata->external_dir) {
        const auto text = entry.metadata->external_dir->generic_u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }
    const auto& globals = GuiSettings();
    if (std::any_of(globals.begin(), globals.end(), [&](const auto& value) { return value.key == key; }))
        return GuiSetting(global, key);
    return field.initial;
}
GuiConfig EffectiveGameConfig(const GameSettings& settings, const GuiConfig& global) {
    auto result = global;
    ValidateGameSettings(settings);
    for (const auto& field : GuiSettings()) if (const auto it = settings.values.find(field.key); it != settings.values.end())
        SetGuiSetting(result, field.key, it->second);
    return result;
}
LibraryEntry EffectiveGameEntry(const LibraryEntry& entry, const GameSettings& settings) {
    auto result = entry;
    ValidateGameSettings(settings);
    if (result.metadata) if (const auto it = settings.values.find("external_dir"); it != settings.values.end()) {
        const auto& text = std::get<std::string>(it->second);
        if (text.empty()) result.metadata->external_dir.reset();
        else result.metadata->external_dir = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }
    return result;
}
}
