#pragma once

// Guest filesystem assembly for run-apk: the read-only base layers a
// Profile declares plus the per-title save sandbox on top (ADR-0020).
// Kept apart from the session loop so both stay readable.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "ogplay/core/logger.h"
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/session/title_profile.h"

namespace ogplay::frontend {

struct SandboxOptions final {
    // Explicit root wins; otherwise the platform user data directory.
    std::optional<std::filesystem::path> directory;
    // Automation and debugging: run with no persistence at all.
    bool ephemeral{};
};

struct SandboxSession final {
    // Null when running ephemeral. Must outlive the VirtualFileSystem.
    std::unique_ptr<runtime::SandboxStore> store;
    std::filesystem::path root;

    [[nodiscard]] std::string Describe() const;
};

// Mounts the Profile's declared external host directory, if any, and checks
// its required manifest entries.
void MountExternalDirectory(
    const session::TitleProfile& profile,
    const std::optional<std::filesystem::path>& directory,
    runtime::VirtualFileSystem& filesystem);

// Opens the save sandbox for this package. Persistence never degrades
// silently: either it opens or the launch fails with a fixable message.
[[nodiscard]] SandboxSession OpenSandbox(const SandboxOptions& options,
                                         const std::string& package,
                                         std::uint32_t version_code);

// Attaches the overlay after every read-only layer is mounted and reports
// the sandbox facts.
void AttachSandbox(const SandboxSession& sandbox,
                   const session::TitleProfile& profile,
                   runtime::VirtualFileSystem& filesystem,
                   core::Logger& logger);

}  // namespace ogplay::frontend
