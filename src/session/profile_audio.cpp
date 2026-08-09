#include "ogplay/session/profile_audio.h"

#include <map>
#include <stdexcept>
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

std::optional<ProfileSoundPoolPath> ResolveProfileSoundPoolPath(
    const TitleProfile& profile, const std::int32_t resource) {
    if (!profile.audio.has_value() ||
        !profile.audio->sound_pool.has_value()) {
        return std::nullopt;
    }
    if (resource < 0) {
        throw ProfileAudioError("SoundPool resource must be non-negative");
    }
    const auto& bank = *profile.audio->sound_pool;
    const auto begin = bank.path_pattern.find("{resource");
    const auto end = begin == std::string::npos
                         ? std::string::npos
                         : bank.path_pattern.find('}', begin);
    if (begin == std::string::npos || end == std::string::npos ||
        bank.path_pattern.find('{', begin + 1U) != std::string::npos ||
        bank.path_pattern.find('}', end + 1U) != std::string::npos) {
        throw ProfileAudioError(
            "SoundPool path pattern must contain one resource placeholder");
    }
    const auto placeholder = bank.path_pattern.substr(begin, end - begin + 1U);
    std::size_t width{};
    if (placeholder != "{resource}") {
        if (placeholder.size() != 13U ||
            !placeholder.starts_with("{resource:0") ||
            placeholder[11] < '1' || placeholder[11] > '9') {
            throw ProfileAudioError("SoundPool resource placeholder is invalid");
        }
        width = static_cast<std::size_t>(placeholder[11] - '0');
    }
    auto number = std::to_string(resource);
    if (number.size() < width) number.insert(0, width - number.size(), '0');
    auto path = bank.path_pattern;
    path.replace(begin, placeholder.size(), number);
    return ProfileSoundPoolPath{bank.source, std::move(path)};
}

std::optional<ProfileAudioPlan> ResolveProfileAudio(
    const TitleProfile& profile,
    const std::span<const ProfileAudioResource> resources) {
    if (!profile.audio.has_value() ||
        !profile.audio->cover_music.has_value()) {
        return std::nullopt;
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
    return ProfileAudioPlan{found->second->contents, music.loop};
}

bool ApplyProfileAudio(
    const TitleProfile& profile,
    const std::span<const ProfileAudioResource> resources,
    audio::MusicPlayer& player) {
    const auto plan = ResolveProfileAudio(profile, resources);
    if (!plan.has_value()) return false;
    player.Play({plan->encoded, plan->loop});
    return true;
}

}  // namespace ogplay::session
