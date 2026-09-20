#pragma once
#include <array>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
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
    std::uint64_t revision{};
    std::uint64_t lease{};

    EncodedAudioSource() = default;
    EncodedAudioSource(std::int32_t value) : resource(value) {}

    [[nodiscard]] static EncodedAudioSource Resource(std::int32_t value) {
        return EncodedAudioSource(value);
    }
    auto operator<=>(const EncodedAudioSource&) const = default;
};

class JavaSoundPoolMixer final {
public:
    struct EncodedResource final {
        std::vector<std::byte> bytes;
        std::shared_ptr<const void> lifetime;

        EncodedResource() = default;
        EncodedResource(std::vector<std::byte> value)
            : bytes(std::move(value)) {}
        EncodedResource(std::vector<std::byte> value,
                        std::shared_ptr<const void> owner)
            : bytes(std::move(value)), lifetime(std::move(owner)) {}
    };

    using EncodedResourceLoader = std::function<EncodedResource(
        const EncodedAudioSource& source)>;

    explicit JavaSoundPoolMixer(EncodedResourceLoader loader = {});
    [[nodiscard]] bool Enabled() const noexcept;
    [[nodiscard]] std::uint32_t CreatePool(std::int32_t max_streams, std::int32_t stream = 3);
    void SetStreamGain(std::int32_t stream, float gain);
    void DestroyPool(std::uint32_t pool);
    [[nodiscard]] bool Load(std::int32_t resource);
    [[nodiscard]] bool Load(const EncodedAudioSource& source);
    [[nodiscard]] std::int32_t LoadSample(std::uint32_t pool,
                                          const EncodedAudioSource& source);
    void Unload(std::int32_t resource);
    void Unload(const EncodedAudioSource& source);
    [[nodiscard]] bool UnloadSample(std::uint32_t pool, std::int32_t sound);
    [[nodiscard]] std::optional<EncodedAudioSource> SampleSource(
        std::uint32_t pool, std::int32_t sound) const;
    [[nodiscard]] std::vector<EncodedAudioSource> PoolSources(
        std::uint32_t pool) const;
    [[nodiscard]] bool Play(JavaSoundPoolKind kind, std::int32_t resource,
                            std::int32_t instance, float volume,
                            bool looping = false);
    [[nodiscard]] bool Play(JavaSoundPoolKind kind,
                            const EncodedAudioSource& source,
                            std::int32_t instance, float volume,
                            bool looping = false);
    [[nodiscard]] std::int32_t PlaySample(std::uint32_t pool, std::int32_t sound,
                                          float left, float right,
                                          std::int32_t priority,
                                          std::int32_t loop, float rate);
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
    void SetStreamVolume(std::int32_t stream, float left, float right);
    void SetStreamLoop(std::int32_t stream, std::int32_t loop);
    void SetStreamPriority(std::int32_t stream, std::int32_t priority);
    void PauseStream(std::int32_t stream);
    void ResumeStream(std::int32_t stream);
    void StopStream(std::int32_t stream);
    void SetPitch(JavaSoundPoolKind kind, std::int32_t resource,
                  std::int32_t instance, float pitch);
    void SetStreamRate(std::int32_t stream, float rate);
    void Reset(JavaSoundPoolKind kind, std::int32_t resource,
               std::int32_t instance);
    void Reset(JavaSoundPoolKind kind, const EncodedAudioSource& source,
               std::int32_t instance);
    void StopAll(JavaSoundPoolKind kind,
                 std::optional<std::int32_t> except_resource = std::nullopt);
    void PauseAll(JavaSoundPoolKind kind);
    void ResumeAll(JavaSoundPoolKind kind);
    void AutoPausePool(std::uint32_t pool);
    void AutoResumePool(std::uint32_t pool);
    void StopAllSounds();
    void Destroy();
    void MixIntoAccumulator(std::span<std::int64_t> accumulator,
                            std::uint32_t output_rate);
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
        std::uint32_t pool{};
        std::int32_t instance{};
        std::int32_t stream{};
        std::int32_t sound{};
        std::int32_t priority{};
        std::int32_t loops_remaining{};
        std::uint64_t age{};
        double position{};
        float left{1.0F};
        float right{1.0F};
        float volume{1.0F};
        float pitch{1.0F};
        bool paused{};
        bool auto_paused{};
        bool looping{};
    };
    struct Pool final {
        std::int32_t max_streams{1};
        std::int32_t next_sound{1};
        std::map<std::int32_t, EncodedAudioSource> samples;
        std::int32_t audio_stream{3};
    };
    [[nodiscard]] std::vector<Voice>::iterator FindVoice(
        JavaSoundPoolKind kind, const EncodedAudioSource& source,
        std::int32_t instance);
    void CollectUnusedResourcesLocked();

    EncodedResourceLoader loader_;
    mutable std::mutex mutex_;
    std::map<EncodedAudioSource, Pcm16Audio> resources_;
    std::map<EncodedAudioSource, std::string> failures_;
    std::map<std::uint32_t, Pool> pools_;
    std::vector<Voice> voices_;
    std::array<float, 10> stream_gains_{1,1,1,1,1,1,1,1,1,1};
    std::uint32_t next_pool_{1};
    std::int32_t next_stream_{1};
    std::uint64_t next_age_{1};
};

}  // namespace ogplay::audio
