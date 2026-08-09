#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/audio/music_player.h"
#include "ogplay/session/title_profile.h"

namespace ogplay::session {

struct ProfileAudioResource final {
    ProfileSource source{ProfileSource::apk};
    std::string path;
    std::vector<std::byte> contents;
};

struct ProfileAudioPlan final {
    std::vector<std::byte> encoded;
    bool loop{};
};

struct ProfileSoundPoolPath final {
    ProfileSource source{ProfileSource::apk};
    std::string path;
};

class ProfileAudioError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::optional<ProfileAudioPlan> ResolveProfileAudio(
    const TitleProfile& profile,
    std::span<const ProfileAudioResource> resources);

[[nodiscard]] bool ApplyProfileAudio(
    const TitleProfile& profile,
    std::span<const ProfileAudioResource> resources,
    audio::MusicPlayer& player);

[[nodiscard]] std::optional<ProfileSoundPoolPath>
ResolveProfileSoundPoolPath(const TitleProfile& profile,
                            std::int32_t resource);

}  // namespace ogplay::session
