#pragma once

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

class JavaSoundPoolMixer final {
public:
    using EncodedResourceLoader =
        std::function<std::vector<std::byte>(std::int32_t resource)>;

    explicit JavaSoundPoolMixer(EncodedResourceLoader loader = {});

    [[nodiscard]] bool Enabled() const noexcept;
    [[nodiscard]] bool Load(std::int32_t resource);
    void Unload(std::int32_t resource);
    [[nodiscard]] bool Play(JavaSoundPoolKind kind, std::int32_t resource,
                            std::int32_t instance, float volume);
    void Pause(JavaSoundPoolKind kind, std::int32_t resource,
               std::int32_t instance);
    void Resume(JavaSoundPoolKind kind, std::int32_t resource,
                std::int32_t instance);
    void Stop(JavaSoundPoolKind kind, std::int32_t resource,
              std::int32_t instance);
    void SetVolume(JavaSoundPoolKind kind, std::int32_t resource,
                   std::int32_t instance, float volume);
    void SetPitch(JavaSoundPoolKind kind, std::int32_t resource,
                  std::int32_t instance, float pitch);
    void Reset(JavaSoundPoolKind kind, std::int32_t resource,
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
    [[nodiscard]] std::size_t LoadedResourceCount() const;
    [[nodiscard]] std::size_t ActiveVoiceCount() const;

private:
    struct Voice final {
        JavaSoundPoolKind kind{JavaSoundPoolKind::pool};
        std::int32_t resource{};
        std::int32_t instance{};
        double position{};
        float volume{1.0F};
        float pitch{1.0F};
        bool paused{};
    };

    [[nodiscard]] std::vector<Voice>::iterator FindVoice(
        JavaSoundPoolKind kind, std::int32_t resource,
        std::int32_t instance);

    EncodedResourceLoader loader_;
    mutable std::mutex mutex_;
    std::map<std::int32_t, Pcm16Audio> resources_;
    std::map<std::int32_t, std::string> failures_;
    std::vector<Voice> voices_;
    std::vector<std::int64_t> mix_scratch_;
};

}  // namespace ogplay::audio
