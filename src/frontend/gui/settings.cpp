#include "ogplay/frontend/gui_settings.h"
#include "ogplay/core/text.h"
#include <algorithm>
#include <stdexcept>

namespace ogplay::frontend {
const std::vector<GuiSettingDefinition>& GuiSettings() {
    // One schema drives persistence, RPC validation and the rendered controls.
    static const std::vector<GuiSettingDefinition> fields{
        {"language", "general", "语言", std::string("zh-CN"), {"zh-CN", "en-US"}, 0, 0, "预留 · 英文翻译尚未接入"},
        {"theme", "general", "主题", std::string("dark"), {"dark", "light", "system"}},
        {"density", "general", "游戏库密度", std::string("comfortable"), {"comfortable", "compact"}},
        {"minimize_on_launch", "general", "启动游戏后最小化", false},
        {"show_exit_log", "general", "异常退出时显示日志末尾", true},
        {"confirm_delete", "general", "移除库中实例前二次确认", true},
        {"profiles_dir", "storage", "Profile 目录覆盖", std::string(), {}, 0, 0, "", true},
        {"default_external_dir", "storage", "默认数据包目录（导入时预填）", std::string(), {}, 0, 0, "", true},
        {"ffmpeg_dir", "storage", "FFmpeg 目录", std::string(), {}, 0, 0, "预留 · 运行时目录配置尚未接入", true},
        {"log_retention", "storage", "每实例保留日志数", std::uint32_t(1), {}, 1, 100, "预留 · 当前只保留 last-run.log"},
        {"angle_backend", "graphics", "ANGLE 后端偏好", std::string("automatic"), {"automatic", "hardware-only", "software-only"}, 0, 0, "预留 · 启动参数尚未接入"},
        {"supersample", "graphics", "默认超采样", std::uint32_t(1), {}, 1, 4},
        {"window_mode", "graphics", "默认窗口模式", std::string("windowed"), {"windowed", "fullscreen"}, 0, 0, "预留 · 运行时未实现"},
        {"scaling", "graphics", "缩放模式", std::string("aspect"), {"aspect", "stretch"}, 0, 0, "预留 · 运行时未实现"},
        {"audio_device", "audio", "输出设备名称", std::string(), {}, 0, 0, "预留 · 运行时设备选择尚未接入"},
        {"volume", "audio", "主音量（%）", std::uint32_t(100), {}, 0, 100, "预留 · 运行时未实现"},
        {"mute", "audio", "静音", false, {}, 0, 0, "预留 · 运行时未实现"},
        {"input_template", "input", "默认输入模板 ID", std::string(), {}, 0, 0, "预留 · 全局模板覆盖尚未接入"},
        {"keymap", "input", "全局键位映射", std::string(), {}, 0, 0, "预留 · 运行时未实现"},
        {"gamepad_deadzone", "input", "手柄死区（%）", std::uint32_t(15), {}, 0, 100, "预留 · 运行时未实现"},
        {"interpreter", "vm", "默认解释器", std::string("profile"), {"profile", "switch", "threaded"}},
        {"clock_mode", "vm", "默认 Clock 模式", std::string("realtime"), {"realtime", "fixed"}, 0, 0, "预留 · 启动参数尚未接入"},
        {"device_preset", "vm", "默认虚拟设备预设", std::string(), {}, 0, 0, "预留 · 运行时未实现"},
        {"network_policy", "network", "默认网络策略", std::string("disabled"), {"disabled", "loopback", "allow"}, 0, 0, "预留 · 不改变运行时网络策略"},
        {"log_level", "diagnostics", "日志级别", std::string("info"), {"trace", "debug", "info", "warn", "error"}, 0, 0, "预留 · 启动参数尚未接入"},
        {"jsonl", "diagnostics", "JSONL 同源输出", false, {}, 0, 0, "预留 · 启动参数尚未接入"},
        {"stall_timeout", "diagnostics", "停滞快照阈值（秒）", std::uint32_t(30), {}, 1, 3600, "预留 · 启动参数尚未接入"},
        {"teardown_timeout", "diagnostics", "退出诊断预算（秒）", std::uint32_t(10), {}, 1, 86400, "预留 · 启动参数尚未接入"},
        {"crash_dump_dir", "diagnostics", "崩溃转储目录", std::string(), {}, 0, 0, "预留 · 运行时未实现", true},
        {"mcp_enabled", "control", "启动时启用 MCP", false},
        {"mcp_port", "control", "MCP 端口", std::uint32_t(15971), {}, 1, 65535},
        {"dashboard_auto_open", "control", "自动打开 Dashboard（需启用 MCP）", false},
    };
    return fields;
}
const GuiSettingDefinition& FindGuiSetting(std::string_view key) {
    const auto& fields = GuiSettings();
    const auto found = std::find_if(fields.begin(), fields.end(), [&](const auto& field) { return field.key == key; });
    if (found == fields.end()) throw std::invalid_argument("unknown setting: " + std::string(key));
    return *found;
}
GuiSettingValue GuiSetting(const GuiConfig& config, std::string_view key) {
    const auto& field = FindGuiSetting(key);
    if (key == "profiles_dir") {
        if (!config.profiles_dir) return std::string();
        const auto text = config.profiles_dir->generic_u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }
    const auto found = config.values.find(key);
    return found == config.values.end() ? field.initial : found->second;
}
void ValidateSettingValue(const GuiSettingDefinition& field, const GuiSettingValue& value) {
    if (field.initial.index() != value.index()) throw std::invalid_argument(field.key + ": invalid type");
    if (const auto number = std::get_if<std::uint32_t>(&value)) {
        if (*number < field.minimum || *number > field.maximum) throw std::invalid_argument(field.key + ": outside allowed range");
    }
    if (const auto text = std::get_if<std::string>(&value)) {
        if (text->size() > 32768 || !core::IsValidUtf8(*text) || std::any_of(text->begin(), text->end(), [](unsigned char c) { return c < 0x20; }))
            throw std::invalid_argument(field.key + ": invalid text");
        if (!field.choices.empty() && std::find(field.choices.begin(), field.choices.end(), *text) == field.choices.end())
            throw std::invalid_argument(field.key + ": unknown choice");
        if (field.directory && !text->empty() && !std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text->data()), text->size())).is_absolute())
            throw std::invalid_argument(field.key + ": directory must be absolute");
    }
}
void SetGuiSetting(GuiConfig& config, std::string_view key, GuiSettingValue value) {
    ValidateSettingValue(FindGuiSetting(key), value);
    if (key == "profiles_dir") {
        const auto& text = std::get<std::string>(value);
        config.profiles_dir = text.empty() ? std::nullopt : std::optional(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size())));
    } else config.values[std::string(key)] = std::move(value);
}
void ValidateGuiConfigValues(const GuiConfig& config) {
    GuiConfig validated;
    SetGuiSetting(validated, "profiles_dir", GuiSetting(config, "profiles_dir"));
    for (const auto& [key, value] : config.values) {
        if (key == "profiles_dir") throw std::invalid_argument("duplicate profiles_dir representation");
        SetGuiSetting(validated, key, value);
    }
}
}
