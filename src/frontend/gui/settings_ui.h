#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <filesystem>
#include <memory>

namespace ogplay::core {
class Logger;
}

namespace ogplay::frontend {

class GuiSettingsUi final {
public:
    GuiSettingsUi(SDL_Window* window, std::filesystem::path library_root,
                  core::Logger& logger);
    ~GuiSettingsUi();

    GuiSettingsUi(const GuiSettingsUi&) = delete;
    GuiSettingsUi& operator=(const GuiSettingsUi&) = delete;

    void Open();
    [[nodiscard]] bool HandleEvent(const SDL_Event& event);
    // Returns true exactly once after a valid config is saved.
    [[nodiscard]] bool Draw();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::frontend
