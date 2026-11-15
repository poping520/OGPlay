#include "frontend/gui/platform.h"

#include "ogplay/app/host.h"

#include <algorithm>
#include <array>
#include <utility>

namespace ogplay::frontend {

namespace {

using Capability = bool GuiPlatform::*;

// Settings owned by one capability; every other key is available everywhere.
constexpr std::array<std::pair<std::string_view, Capability>, 22> kSettingCapabilities{{
    {"keymap", &GuiPlatform::key_editor},
    {"gamepad_deadzone", &GuiPlatform::key_editor},
    {"dashboard_auto_open", &GuiPlatform::dashboard},
    {"mcp_enabled", &GuiPlatform::mcp},
    {"mcp_port", &GuiPlatform::mcp},
    {"mcp_manual_step", &GuiPlatform::mcp},
    {"window_mode", &GuiPlatform::desktop_window},
    {"minimize_on_launch", &GuiPlatform::desktop_window},
    {"running_icon", &GuiPlatform::desktop_window},
    {"supersample", &GuiPlatform::desktop_window},
    {"angle_backend", &GuiPlatform::desktop_window},
    {"profiles_dir", &GuiPlatform::path_settings},
    {"default_external_dir", &GuiPlatform::path_settings},
    {"crash_dump_dir", &GuiPlatform::path_settings},
    {"external_dir", &GuiPlatform::path_settings},
    {"interpreter", &GuiPlatform::developer},
    {"clock_mode", &GuiPlatform::developer},
    {"log_level", &GuiPlatform::developer},
    {"jsonl", &GuiPlatform::developer},
    {"stall_timeout", &GuiPlatform::developer},
    {"teardown_timeout", &GuiPlatform::developer},
    {"ephemeral_sandbox", &GuiPlatform::developer},
}};

constexpr std::array<std::pair<std::string_view, Capability>, 11> kMethodCapabilities{{
    {"library.resume", &GuiPlatform::single_game},
    {"library.stop", &GuiPlatform::single_game},
    {"controls.open", &GuiPlatform::key_editor},
    {"dashboard.list", &GuiPlatform::dashboard},
    {"dashboard.open", &GuiPlatform::dashboard},
    {"library.open_dir", &GuiPlatform::open_directory},
    {"settings.open_dir", &GuiPlatform::open_directory},
    {"settings.icon_preview", &GuiPlatform::desktop_window},
    {"library.upload.begin", &GuiPlatform::file_upload},
    {"library.upload.chunk", &GuiPlatform::file_upload},
    {"library.upload.finish", &GuiPlatform::file_upload},
}};

template <std::size_t N>
bool Available(const GuiPlatform& platform, const std::array<std::pair<std::string_view, Capability>, N>& table,
               std::string_view key) noexcept {
    const auto found = std::find_if(table.begin(), table.end(), [&](const auto& item) { return item.first == key; });
    return found == table.end() || platform.*(found->second);
}

}  // namespace

GuiPlatform GuiPlatform::Desktop() {
    return {.name = app::HostPlatformName()};
}

GuiPlatform GuiPlatform::Android() {
    return {.name = app::HostPlatformName(),
            .game_executable = false,
            .key_editor = false,
            .dashboard = false,
            .mcp = false,
            .launch_modes = false,
            .open_directory = false,
            .desktop_window = false,
            .path_settings = false,
            .file_upload = false,
            .developer = false,
            .single_game = true};
}

bool SettingAvailable(const GuiPlatform& platform, std::string_view key) noexcept {
    return Available(platform, kSettingCapabilities, key);
}

bool MethodAvailable(const GuiPlatform& platform, std::string_view method) noexcept {
    return Available(platform, kMethodCapabilities, method);
}

}  // namespace ogplay::frontend
