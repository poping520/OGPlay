#pragma once

// Guest filesystem assembly for Android standard storage paths.
// Kept apart from the session loop so both stay readable.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ogplay/core/logger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::frontend {

struct SandboxOptions final {
    // Explicit root wins; otherwise the platform user data directory.
    std::optional<std::filesystem::path> directory;
    // Automation and debugging: run with no persistence at all.
    bool ephemeral{};
    // Advanced override selected by the library. Direct APK launches resolve
    // a unique existing instance or allocate the package-named first instance.
    std::optional<std::string> installation_id;
};

struct SandboxSession final {
    // Null when running ephemeral. Must outlive the VirtualFileSystem.
    std::unique_ptr<runtime::SandboxStore> store;
    std::filesystem::path root;
    // Exact instance identity propagated to both the sandbox and guest
    // platform facts. Ephemeral sessions use a fresh non-persistent value.
    std::string installation_id;
    // Per-sandbox API 19 identity. Never derived from host hardware.
    std::string android_id;

    [[nodiscard]] std::string Describe() const;
};

// ADR-0146: collects /sdcard read lookups that found nothing while attached and
// writes them with the mount facts. Destruction writes too, so startup faults
// still leave a report; declare it after (destroy it before) the filesystem.
class MissingDataRecorder final {
public:
    MissingDataRecorder(runtime::VirtualFileSystem& filesystem, core::Logger& logger,
                        std::filesystem::path report,
                        std::optional<std::filesystem::path> external_directory,
                        std::string external_guest_directory);
    ~MissingDataRecorder();
    MissingDataRecorder(const MissingDataRecorder&) = delete;
    MissingDataRecorder& operator=(const MissingDataRecorder&) = delete;
    // Drops paths that exist by now (files the game created itself); idempotent.
    void Write() noexcept;

private:
    struct State;
    runtime::VirtualFileSystem& filesystem_;
    core::Logger& logger_;
    std::shared_ptr<State> state_;
    std::filesystem::path report_;
    std::optional<std::filesystem::path> external_directory_;
    std::string external_guest_directory_;
    bool written_{};
};

// Mounts external data at /sdcard or an explicitly supplied storage subdirectory.
void MountExternalDirectory(
    const std::optional<std::filesystem::path>& directory,
    runtime::VirtualFileSystem& filesystem,
    const std::optional<std::string>& guest_directory = std::nullopt);
// Opens the save sandbox for this package. Persistence never degrades
// silently: either it opens or the launch fails with a fixable message.
[[nodiscard]] SandboxSession OpenSandbox(const SandboxOptions& options,
                                         const std::string& package,
                                         std::uint32_t version_code);

// Attaches the overlay after every read-only layer is mounted and reports
// the sandbox facts.
void AttachSandbox(const SandboxSession& sandbox,
                   const std::string& package,
                   runtime::VirtualFileSystem& filesystem,
                   core::Logger& logger);

}  // namespace ogplay::frontend
