#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ogplay::frontend {
struct GuiDashboard final {
    std::string installation_id;
    std::uint64_t process_id{};
    std::optional<std::uint16_t> port;
    std::string status{"disabled"};
    std::string detail{"启动前在游戏设置中启用 MCP。"};
    // Key editor (ADR-0134): every launched game serves it; the token never leaves the host.
    std::string editor_status{"disabled"};
    std::string editor_detail{"游戏运行后可打开按键设置。"};
};
struct DashboardProbe final { bool ready{}; std::string detail; };
struct KeyEditorProbe final { bool ready{}; std::uint64_t edit_requests{}; std::string detail; };
// Worker-only, loopback-only, bounded time/body. Never executes guest or controls.
[[nodiscard]] DashboardProbe ProbeDashboard(std::uint16_t port, std::uint64_t process_id);
[[nodiscard]] DashboardProbe DecodeDashboardProbe(std::string_view response, std::uint64_t process_id);
// Launcher poll: control.status with the launch token; edit_requests counts in-game F2.
[[nodiscard]] KeyEditorProbe ProbeKeyEditor(std::uint16_t port, std::string_view token);
[[nodiscard]] KeyEditorProbe DecodeKeyEditorProbe(std::string_view response);
// A free loopback port for --key-editor-port (released before the game binds it).
[[nodiscard]] std::uint16_t PickLoopbackPort();
}
