#pragma once

#include <filesystem>
#include <optional>

namespace ogplay::frontend {

struct UserDataLocation {
    std::filesystem::path root;
    bool portable{};
};

// Selects the root without creating or writing to it.
[[nodiscard]] std::optional<UserDataLocation>
ResolveUserDataLocation(const std::filesystem::path& executable_directory, bool portable_supported,
                        const std::optional<std::filesystem::path>& platform_root);

// Creates and probes a portable root. Failures include the path and the recovery steps.
void EnsureWritableUserDataRoot(const std::filesystem::path& root);

// Resolves the current host's root and validates it if portable mode is active.
[[nodiscard]] std::optional<UserDataLocation> HostUserDataLocation();

// nullopt when the host environment does not say where user data belongs;
// callers report that and offer an explicit flag rather than guessing.
[[nodiscard]] std::optional<std::filesystem::path> UserDataDirectory();

// <user data>/sandbox: per-title save sandboxes (ADR-0020).
[[nodiscard]] std::optional<std::filesystem::path> DefaultSandboxRoot();

// <user data>/library: the launcher game library (docs/design/frontend.md).
[[nodiscard]] std::optional<std::filesystem::path> DefaultLibraryRoot();

}  // namespace ogplay::frontend
