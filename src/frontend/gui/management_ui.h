#pragma once

#include <memory>

#include "ogplay/frontend/gui_model.h"

namespace ogplay::core {
class Logger;
}

namespace ogplay::frontend {

class GuiManagementUi final {
public:
    GuiManagementUi(LibraryStore& store, core::Logger& logger);
    ~GuiManagementUi();

    GuiManagementUi(const GuiManagementUi&) = delete;
    GuiManagementUi& operator=(const GuiManagementUi&) = delete;

    void OpenDelete(const LibraryEntry& entry, bool running);
    // Returns true exactly once after an entry is removed.
    [[nodiscard]] bool Draw();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::frontend
