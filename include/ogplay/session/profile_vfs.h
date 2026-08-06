#pragma once

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

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
};

class ProfileVfsError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] ProfileVfsAssembly AssembleProfileVfs(
    const TitleProfile& profile, std::span<const ProfileVfsMountInput> inputs);

}  // namespace ogplay::session
