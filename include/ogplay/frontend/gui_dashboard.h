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
};
struct DashboardProbe final { bool ready{}; std::string detail; };
// Worker-only, loopback-only, bounded time/body. Never executes guest or controls.
[[nodiscard]] DashboardProbe ProbeDashboard(std::uint16_t port, std::uint64_t process_id);
[[nodiscard]] DashboardProbe DecodeDashboardProbe(std::string_view response, std::uint64_t process_id);
}
