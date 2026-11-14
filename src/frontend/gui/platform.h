#pragma once

#include <string>
#include <string_view>

namespace ogplay::frontend {

// What the launcher host offers on this platform. One declaration drives the RPC methods, the setting
// fields and the launch plan; the page reads the same facts from host.features and draws only those.
struct GuiPlatform final {
    std::string name;            // the host OS: Windows, Darwin, Linux or Android
    bool game_executable{true};  // launches spawn `<exe> --run-game`; false: argv holds only the game options
    bool key_editor{true};       // controls.open, editor token/port, keymap settings (ADR-0134)
    bool dashboard{true};        // dashboard.list/open, dashboard_auto_open
    bool mcp{true};              // mcp_* settings
    bool launch_modes{true};     // preflight and diagnostic launches
    bool open_directory{true};   // library.open_dir, settings.open_dir
    bool desktop_window{true};   // window and taskbar settings, running icon preview
    // Host directories kept by reference: typed directory settings and data packs linked in place. Without it a
    // picked directory may be a temporary copy (Android SAF), so data packs are always copied into the library.
    bool path_settings{true};
    bool file_upload{true};  // library.upload.* (drag and drop)
    bool developer{true};    // interpreter, clock, log and timeout tuning

    [[nodiscard]] static GuiPlatform Desktop();
    [[nodiscard]] static GuiPlatform Android();
};

// False for a setting key that belongs to a capability this platform leaves out.
[[nodiscard]] bool SettingAvailable(const GuiPlatform& platform, std::string_view key) noexcept;
// False for an RPC method that belongs to a capability this platform leaves out.
[[nodiscard]] bool MethodAvailable(const GuiPlatform& platform, std::string_view method) noexcept;

}  // namespace ogplay::frontend
