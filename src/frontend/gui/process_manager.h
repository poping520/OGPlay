#pragma once

#include <SDL3/SDL_process.h>

#include <filesystem>
#include <future>
#include "ogplay/frontend/gui_dashboard.h"
#include <memory>
#include <string_view>
#include <vector>

#include "ogplay/frontend/gui_launch.h"

namespace ogplay::core {
class Logger;
}

namespace ogplay::frontend {

class GuiProcessManager final {
public:
    explicit GuiProcessManager(core::Logger& logger);
    ~GuiProcessManager();

    GuiProcessManager(const GuiProcessManager&) = delete;
    GuiProcessManager& operator=(const GuiProcessManager&) = delete;

    void Launch(const LaunchPlan& plan);
    [[nodiscard]] std::vector<GameExit> Poll();
    [[nodiscard]] bool IsRunning(std::string_view package) const noexcept;
    [[nodiscard]] std::vector<std::string> RunningPackages() const;
    [[nodiscard]] std::vector<GuiDashboard> Dashboards() const;
    [[nodiscard]] std::uint16_t DashboardPort(std::string_view instance) const;
    [[nodiscard]] std::vector<std::string> TakeAutoOpen();

private:
    struct Active final {
        std::string package;
        SDL_Process* process{};
        GuiDashboard dashboard;
        std::future<DashboardProbe> probe;
        std::uint64_t next_probe{};
        bool auto_open{};
    };
    core::Logger& logger_;
    LaunchTracker tracker_;
    std::vector<Active> active_;
    std::vector<std::future<DashboardProbe>> retired_probes_;
};

[[nodiscard]] std::filesystem::path FindSiblingCliExecutable();

}  // namespace ogplay::frontend
