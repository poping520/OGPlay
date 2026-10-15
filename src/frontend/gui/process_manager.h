#pragma once

#include <SDL3/SDL_process.h>

#include <filesystem>
#include <future>
#include "frontend/gui/dashboard.h"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "frontend/gui/launch.h"

namespace ogplay::app {
class Logger;
}

namespace ogplay::frontend {

class GuiProcessManager final {
public:
    explicit GuiProcessManager(app::Logger& logger);
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
    // Key editor of a running instance (ADR-0134); throws while it is not ready.
    struct KeyEditorAddress final { std::uint16_t port{}; std::string token; };
    [[nodiscard]] KeyEditorAddress KeyEditor(std::string_view instance) const;
    // Instances whose game asked for the editor with F2 since the last call.
    [[nodiscard]] std::vector<std::string> TakeKeyEditorRequests();

private:
    struct Active final {
        std::string package;
        SDL_Process* process{};
        GuiDashboard dashboard;
        std::future<DashboardProbe> probe;
        std::uint64_t next_probe{};
        bool auto_open{};
        std::optional<std::uint16_t> editor_port;
        std::string control_token;
        std::future<KeyEditorProbe> editor_probe;
        std::uint64_t next_editor_probe{};
        std::uint64_t edit_requests{};
        bool editor_requested{};
    };
    app::Logger& logger_;
    LaunchTracker tracker_;
    std::vector<Active> active_;
    std::vector<std::future<DashboardProbe>> retired_probes_;
    std::vector<std::future<KeyEditorProbe>> retired_editor_probes_;
};


}  // namespace ogplay::frontend
