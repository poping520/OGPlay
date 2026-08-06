#include "ogplay/session/profile_audio.h"

#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace ogplay::session {
namespace {

struct ResourceKey final {
    ProfileSource source{ProfileSource::apk};
    std::string path;

    bool operator<(const ResourceKey& other) const {
        if (source != other.source) return source < other.source;
        return path < other.path;
    }
};

[[nodiscard]] std::string ResourceName(const ProfileSource source,
                                       const std::string_view path) {
    return std::string(ToString(source)) + ":" + std::string(path);
}

}  // namespace

bool ApplyProfileAudio(
    const TitleProfile& profile,
    const std::span<const ProfileAudioResource> resources,
    audio::MusicPlayer& player) {
    if (!profile.audio.has_value() ||
        !profile.audio->cover_music.has_value()) {
        return false;
    }

    std::map<ResourceKey, const ProfileAudioResource*> indexed;
    for (const auto& resource : resources) {
        if (resource.path.empty()) {
            throw ProfileAudioError("Profile audio resource path is empty");
        }
        if (resource.contents.empty()) {
            throw ProfileAudioError("Profile audio resource is empty: " +
                                    ResourceName(resource.source, resource.path));
        }
        const auto [unused, inserted] = indexed.emplace(
            ResourceKey{resource.source, resource.path}, &resource);
        static_cast<void>(unused);
        if (!inserted) {
            throw ProfileAudioError("duplicate Profile audio resource: " +
                                    ResourceName(resource.source, resource.path));
        }
    }

    const auto& music = *profile.audio->cover_music;
    const auto found = indexed.find(ResourceKey{music.source, music.path});
    if (found == indexed.end()) {
        throw ProfileAudioError("Profile cover music resource is missing: " +
                                ResourceName(music.source, music.path));
    }
    player.Play({found->second->contents, music.loop});
    return true;
}

}  // namespace ogplay::session
