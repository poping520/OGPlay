#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ogplay/audio/java_sound_pool.h"
#include "ogplay/audio/ogg_vorbis.h"

namespace ogplay::audio {

struct EncodedAudioSource final {
    enum class Kind : std::uint8_t { resource, apk_entry, vfs_path };
    Kind kind{Kind::resource};
    std::int32_t resource{};
    std::string name;
    std::uint64_t offset{};
    std::uint64_t length{};

    EncodedAudioSource() = default;
    EncodedAudioSource(std::int32_t value) : resource(value) {}

    [[nodiscard]] static EncodedAudioSource Resource(std::int32_t value) {
        return EncodedAudioSource(value);
    }
    auto operator<=>(const EncodedAudioSource&) const = default;
};

class JavaSoundPoolMixer final {
public:
    using EncodedResourceLoader = std::function<std::vector<std::byte>(
        const EncodedAudioSource& source)>;

    explicit JavaSoundPoolMixer(EncodedResourceLoader loader = {});
    [[nodiscard]] bool Enabled() const noexcept;
    [[nodiscard]] bool Load(std::int32_t resource);
    [[nodiscard]] bool Load(const EncodedAudioSource& source);
    void Unload(std::int32_t resource);
    void Unload(const EncodedAudioSource& source);
    [[nodiscard]] bool Play(JavaSoundPoolKind kind, std::int32_t resource,
                            std::int32_t instance, float volume,
                            bool looping = false);
    [[nodiscard]] bool Play(JavaSoundPoolKind kind,
                            const EncodedAudioSource& source,
                            std::int32_t instance, float volume,
                            bool looping = false);
    void Pause(JavaSoundPoolKind kind, std::int32_t resource,
               std::int32_t instance);
    void Pause(JavaSoundPoolKind kind, const EncodedAudioSource& source,
               std::int32_t instance);
    void Resume(JavaSoundPoolKind kind, std::int32_t resource,
                std::int32_t instance);
    void Resume(JavaSoundPoolKind kind, const EncodedAudioSource& source,
                std::int32_t instance);
    void Stop(JavaSoundPoolKind kind, std::int32_t resource,
              std::int32_t instance);
    void Stop(JavaSoundPoolKind kind, const EncodedAudioSource& source,
              std::int32_t instance);
    void SetVolume(JavaSoundPoolKind kind, std::int32_t resource,
                   std::int32_t instance, float volume);
    void SetVolume(JavaSoundPoolKind kind, const EncodedAudioSource& source,
                   std::int32_t instance, float volume);
    void SetPitch(JavaSoundPoolKind kind, std::int32_t resource,
                  std::int32_t instance, float pitch);
    void Reset(JavaSoundPoolKind kind, std::int32_t resource,
               std::int32_t instance);
    void Reset(JavaSoundPoolKind kind, const EncodedAudioSource& source,
               std::int32_t instance);
    void StopAll(JavaSoundPoolKind kind,
                 std::optional<std::int32_t> except_resource = std::nullopt);
    void PauseAll(JavaSoundPoolKind kind);
    void ResumeAll(JavaSoundPoolKind kind);
    void StopAllSounds();
    void Destroy();
    [[nodiscard]] std::size_t RenderStereoPcm16(
        std::span<std::int16_t> output, std::uint32_t output_rate);
    [[nodiscard]] std::optional<std::string> LoadFailure(
        std::int32_t resource) const;
    [[nodiscard]] std::optional<std::string> LoadFailure(
        const EncodedAudioSource& source) const;
    [[nodiscard]] std::size_t LoadedResourceCount() const;
    [[nodiscard]] std::size_t ActiveVoiceCount() const;

private:
    struct Voice final {
        JavaSoundPoolKind kind{JavaSoundPoolKind::pool};
        EncodedAudioSource source;
        std::int32_t instance{};
        double position{};
        float volume{1.0F};
        float pitch{1.0F};
        bool paused{};
        bool looping{};
    };
    [[nodiscard]] std::vector<Voice>::iterator FindVoice(
        JavaSoundPoolKind kind, const EncodedAudioSource& source,
        std::int32_t instance);

    EncodedResourceLoader loader_;
    mutable std::mutex mutex_;
    std::map<EncodedAudioSource, Pcm16Audio> resources_;
    std::map<EncodedAudioSource, std::string> failures_;
    std::vector<Voice> voices_;
    std::vector<std::int64_t> mix_scratch_;
};

}  // namespace ogplay::audio
