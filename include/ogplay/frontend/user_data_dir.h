#pragma once

// What OGPlay keeps under the host user-data directory (roadmap 08 ·
// naming). The platform root itself comes from hal::HostUserDataDirectory;
// this header only names the subdirectories the frontend owns.

#include <filesystem>
#include <optional>

namespace ogplay::frontend {

// nullopt when the host environment does not say where user data belongs;
// callers report that and offer an explicit flag rather than guessing.
[[nodiscard]] std::optional<std::filesystem::path> UserDataDirectory();

// <user data>/sandbox: per-title save sandboxes (ADR-0020).
[[nodiscard]] std::optional<std::filesystem::path> DefaultSandboxRoot();

// <user data>/library: the launcher game library (docs/design/launcher/).
[[nodiscard]] std::optional<std::filesystem::path> DefaultLibraryRoot();

}  // namespace ogplay::frontend
