#pragma once

#include <SDL3/SDL_process.h>

#include <filesystem>
#include <memory>
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
    [[nodiscard]] std::vector<std::string> RunningPackages() const;

private:
    struct Active final {
        std::string package;
        SDL_Process* process{};
    };
    core::Logger& logger_;
    LaunchTracker tracker_;
    std::vector<Active> active_;
};

[[nodiscard]] std::filesystem::path FindSiblingCliExecutable();

}  // namespace ogplay::frontend
