// User-data directory policy (roadmap 08 · naming). The per-platform root
// itself comes from hal, which is the only place allowed to know about
// platform differences; this file only decides what OGPlay puts under it.

#include "ogplay/frontend/user_data_dir.h"

#include "ogplay/hal/host_environment.h"

namespace ogplay::frontend {

std::optional<std::filesystem::path> UserDataDirectory() {
    return hal::HostUserDataDirectory();
}

std::optional<std::filesystem::path> DefaultSandboxRoot() {
    const auto base = UserDataDirectory();
    if (!base.has_value()) return std::nullopt;
    return *base / "sandbox";
}

std::optional<std::filesystem::path> DefaultLibraryRoot() {
    const auto base = UserDataDirectory();
    if (!base.has_value()) return std::nullopt;
    return *base / "library";
}

}  // namespace ogplay::frontend
