#pragma once

#include <functional>
#include <string>
#include <vector>

#include "frontend/gui/rpc.h"

namespace ogplay::app {
class Logger;
}

namespace ogplay::frontend {

// The launcher pieces every platform shares: bundled demos, library facts, Profile and credit reads.
// A platform host adds what only it can do (launching, pickers, windows) to the GuiRpcHost it returns.
class LauncherServices final {
public:
    LauncherServices(LibraryStore& store, app::Logger& logger);
    LauncherServices(const LauncherServices&) = delete;
    LauncherServices& operator=(const LauncherServices&) = delete;

    // running lists the instances the host itself tracks; the library run locks add the rest.
    [[nodiscard]] GuiRpcHost Host(std::function<std::vector<std::string>()> running);
    void SeedDemos();
    // The configured launcher language, the system one when the configuration cannot be read.
    [[nodiscard]] UiLanguage Language() const;

private:
    LibraryStore& store_;
    app::Logger& logger_;
    std::vector<app::ApkImportAnalysis> demos_;
};

}  // namespace ogplay::frontend
