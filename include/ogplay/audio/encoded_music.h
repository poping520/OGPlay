#pragma once

#include <cstdint>
#include <array>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

#include "ogplay/audio/encoded_audio_stream.h"

namespace ogplay::audio {

class EncodedMusicMixer final {
public:
    static constexpr std::size_t kMaximumVoices = 32;
    static constexpr std::size_t kMaximumMusicBytes = 128U * 1024U * 1024U;
    enum class PrepareStatus { pending, ready, failed };
    enum class StartStatus { started, not_prepared, voice_limit };

    struct PlaybackStats {
        std::uint64_t created_players{};
        std::uint64_t peak_reserved_voices{};
    };
    explicit EncodedMusicMixer(ExternalAudioDecoder decoder = {});
    [[nodiscard]] std::uint32_t Create();
    void Destroy(std::uint32_t player);
    void Reset(std::uint32_t player);
    [[nodiscard]] bool SetEncoded(std::uint32_t player,
                                  std::vector<std::byte> encoded);
    [[nodiscard]] bool SetSource(
        std::uint32_t player,
        std::shared_ptr<const EncodedAudioDataSource> source);
    [[nodiscard]] bool Prepare(std::uint32_t player);
    [[nodiscard]] bool BeginPrepare(std::uint32_t player);
    [[nodiscard]] PrepareStatus PollPrepare(std::uint32_t player);
    [[nodiscard]] std::optional<AudioDecodeFailure> PrepareFailure(std::uint32_t player) const;
    StartStatus Start(std::uint32_t player);
    void Pause(std::uint32_t player);
    void Stop(std::uint32_t player);
    void SeekMs(std::uint32_t player, std::int32_t milliseconds);
    void SetLooping(std::uint32_t player, bool looping);
    void SetVolume(std::uint32_t player, float left, float right);
    void SetAudioStream(std::uint32_t player, std::int32_t stream);
    void SetStreamGain(std::int32_t stream, float gain);
    [[nodiscard]] bool IsPlaying(std::uint32_t player) const;
    [[nodiscard]] bool HasEncoded(std::uint32_t player) const;
    [[nodiscard]] bool AnyPlaying() const;
    [[nodiscard]] bool IsLooping(std::uint32_t player) const;
    [[nodiscard]] bool Completed(std::uint32_t player);
    [[nodiscard]] bool TakeDecodeFailure(std::uint32_t player);
    [[nodiscard]] std::int32_t DurationMs(std::uint32_t player) const;
    [[nodiscard]] std::int32_t PositionMs(std::uint32_t player) const;
    [[nodiscard]] std::size_t CachedDecodedBytes() const;
    [[nodiscard]] PlaybackStats Stats() const;
    void MixIntoAccumulator(std::span<std::int64_t> accumulator,
                            std::uint32_t output_rate);

private:
    struct PrepareTask final {
        std::mutex mutex;
        std::condition_variable ready;
        std::unique_ptr<EncodedAudioStream> stream;
        std::optional<AudioDecodeFailure> failure;
        bool done{};
        // Declared last: stop and join before destroying task data.
        std::jthread worker;
        void Cancel() {
            if (worker.joinable()) { worker.request_stop(); worker.join(); }
            std::scoped_lock lock(mutex);
            stream.reset();
        }
    };

    struct Player final {
        std::shared_ptr<const EncodedAudioDataSource> source;
        std::unique_ptr<EncodedAudioStream> decoder;
        std::uint32_t sample_rate{};
        std::uint8_t channels{1};
        std::size_t total_frames{};
        double position{};
        float left{1.0F};
        float right{1.0F};
        bool looping{};
        bool playing{};
        bool prepared{};
        bool has_voice{};
        bool completed{};
        bool decode_failed{};
        std::int32_t audio_stream{3};
        std::uint64_t generation{};
        std::shared_ptr<PrepareTask> prepare_task;
        std::optional<AudioDecodeFailure> prepare_failure;
    };

    mutable std::mutex mutex_;
    ExternalAudioDecoder external_decoder_;
    std::map<std::uint32_t, Player> players_;
    std::uint64_t reserved_voices_{};
    PlaybackStats stats_;
    std::array<float, 10> stream_gains_{1,1,1,1,1,1,1,1,1,1};
    std::uint32_t next_{1};
};

}  // namespace ogplay::audio
