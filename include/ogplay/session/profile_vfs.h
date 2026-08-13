#pragma once

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/session/title_profile.h"

namespace ogplay::session {

struct ProfileVfsMountInput final {
    std::string guest;
    ProfileSource source{ProfileSource::external};
    std::vector<runtime::VfsMountEntry> entries;
};

struct ProfileManifestStatus final {
    std::string path;
    bool required{};
    bool present{};
};

struct ProfileVfsAssembly final {
    std::unique_ptr<runtime::VirtualFileSystem> filesystem;
    std::optional<std::string> working_directory;
    std::vector<ProfileManifestStatus> manifest;
    // Writable namespace the sandbox was attached to; empty when running
    // without persistence.
    std::vector<std::string> writable_roots;
};

// The guest paths a title may write to (ADR-0020 02 §1). Built from the
// manifest package identity, never from user input.
[[nodiscard]] std::vector<std::string> ProfileWritableRoots(
    const TitleProfile& profile);

class ProfileVfsError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// sandbox == nullptr runs without persistence (the --ephemeral-sandbox and
// automation path); otherwise the overlay is attached after every read-only
// base layer is mounted, so the guest sees its saved state.
[[nodiscard]] ProfileVfsAssembly AssembleProfileVfs(
    const TitleProfile& profile, std::span<const ProfileVfsMountInput> inputs,
    runtime::SandboxStore* sandbox = nullptr);

}  // namespace ogplay::session
