#pragma once

#include <span>
#include <stdexcept>
#include <vector>

#include "ogplay/session/profile_audio.h"
#include "ogplay/session/profile_vfs.h"

namespace ogplay::session {

class ProfileAssetBundleError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ProfileAssetBundle final {
public:
    ProfileAssetBundle(std::vector<ProfileVfsMountInput> vfs_mounts,
                       std::vector<ProfileAudioResource> audio_resources);

    [[nodiscard]] std::span<const ProfileVfsMountInput>
    VfsMounts() const noexcept;
    [[nodiscard]] std::span<const ProfileAudioResource>
    AudioResources() const noexcept;

private:
    std::vector<ProfileVfsMountInput> vfs_mounts_;
    std::vector<ProfileAudioResource> audio_resources_;
};

}  // namespace ogplay::session
