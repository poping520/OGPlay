#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/app/library.h"

namespace ogplay::frontend {
using namespace ogplay::app;

struct LaunchPlan final {
    std::string package;
    std::vector<std::string> argv;
    std::filesystem::path log_path;
    std::optional<std::uint16_t> mcp_port;
    bool dashboard_auto_open{};
    // Key editor service (ADR-0134): the MCP port when MCP is on, else --key-editor-port.
    std::optional<std::uint16_t> key_editor_port;
    // Per-launch editor token, handed to the game as OGPLAY_CONTROL_TOKEN and never in argv.
    std::string control_token;
};




[[nodiscard]] LaunchPlan BuildLaunchPlan(
    const std::filesystem::path& game_executable,
    const std::filesystem::path& library_root,
    const LibraryEntry& entry, const LibraryConfig& config, LaunchMode mode = LaunchMode::normal);

struct GameExit final {
    std::string package;
    int exit_code{};
    std::filesystem::path log_path;
};

class LaunchTracker final {
public:
    void Begin(std::string package, std::filesystem::path log_path);
    [[nodiscard]] GameExit Finish(std::string_view package, int exit_code);
    [[nodiscard]] bool IsRunning(std::string_view package) const noexcept;
    [[nodiscard]] std::vector<std::string> RunningPackages() const;

private:
    struct Active final {
        std::string package;
        std::filesystem::path log_path;
    };
    std::vector<Active> active_;
};

[[nodiscard]] std::string ReadLogTail(
    const std::filesystem::path& path, std::size_t maximum_lines = 20,
    std::size_t maximum_bytes = 16U * 1024U);

}  // namespace ogplay::frontend
