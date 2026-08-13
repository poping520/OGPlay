#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ogplay/frontend/gui_model.h"

namespace ogplay::core {
class Logger;
}

namespace ogplay::frontend {

// ImGui/SDL controller for GUI-5. File dialog callbacks only publish an SDL
// event; all model mutation and GL work stays on the GUI main thread.
class GuiImportUi final {
public:
    GuiImportUi(SDL_Window* window, LibraryStore& store, core::Logger& logger);
    ~GuiImportUi();

    GuiImportUi(const GuiImportUi&) = delete;
    GuiImportUi& operator=(const GuiImportUi&) = delete;

    void OpenApkDialog();
    [[nodiscard]] bool HandleEvent(const SDL_Event& event);
    // Returns true exactly once after an entry is successfully published.
    [[nodiscard]] bool Draw();
    // Empty means the configured/default Profile catalog reloaded successfully.
    [[nodiscard]] std::optional<std::string> ReloadProfiles();
    [[nodiscard]] std::vector<std::string> ExternalRequiredPackages(
        const std::vector<LibraryEntry>& entries) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::frontend
