#pragma once

#include <cstddef>
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

class ProfileAudioError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] bool ApplyProfileAudio(
    const TitleProfile& profile,
    std::span<const ProfileAudioResource> resources,
    audio::MusicPlayer& player);

}  // namespace ogplay::session
