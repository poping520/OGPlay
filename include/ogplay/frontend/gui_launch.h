#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/frontend/gui_model.h"

namespace ogplay::frontend {

struct LaunchPlan final {
    std::string package;
    std::vector<std::string> argv;
    std::filesystem::path log_path;
};

[[nodiscard]] std::filesystem::path LauncherSandboxRoot(
    const std::filesystem::path& library_root);

[[nodiscard]] LaunchPlan BuildLaunchPlan(
    const std::filesystem::path& cli_executable,
    const std::filesystem::path& library_root,
    const LibraryEntry& entry, const GuiConfig& config);

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
