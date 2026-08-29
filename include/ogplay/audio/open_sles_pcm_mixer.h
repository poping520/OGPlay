#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <span>
#include <vector>

namespace ogplay::audio {

enum class OpenSlesPlayState : std::uint8_t { stopped, paused, playing };

struct OpenSlesPcmFormat final {
    std::uint32_t sample_rate{};
    std::uint8_t channels{};
    std::uint8_t bits_per_sample{};
};

struct OpenSlesQueueState final {
    std::uint32_t count{};
    std::uint32_t play_index{};
};

struct OpenSlesConsumedBuffer final {
    std::uint64_t player{};
    std::uint64_t sequence{};
};

enum class OpenSlesEnqueueResult : std::uint8_t {
    enqueued,
    interrupted,
    player_destroyed,
};

class OpenSlesPcmMixer final {
public:
    using PlayerId = std::uint64_t;

    [[nodiscard]] PlayerId CreatePlayer(OpenSlesPcmFormat format,
                                        std::uint8_t queue_capacity);
    void DestroyPlayer(PlayerId player) noexcept;
    [[nodiscard]] bool HasPlayer(PlayerId player) const;
    [[nodiscard]] bool Enqueue(PlayerId player, std::span<const std::byte> pcm);
    // AudioTrack MODE_STREAM writes block against its Android buffer-size
    // byte budget. The item capacity remains only a bounded memory guard.
    [[nodiscard]] OpenSlesEnqueueResult EnqueueBlocking(
        PlayerId player, std::span<const std::byte> pcm,
        std::size_t maximum_queued_bytes);
    [[nodiscard]] std::size_t QueuedBytes(PlayerId player) const;
    [[nodiscard]] std::size_t BlockingWriterCount() const noexcept;
    // Process teardown is sticky: wake current writers and reject later ones.
    [[nodiscard]] std::size_t InterruptBlockingWaits() noexcept;
    void Clear(PlayerId player);
    [[nodiscard]] OpenSlesQueueState QueueState(PlayerId player) const;
    void SetPlayState(PlayerId player, OpenSlesPlayState state);
    [[nodiscard]] OpenSlesPlayState PlayState(PlayerId player) const;
    [[nodiscard]] std::uint32_t PositionMillis(PlayerId player) const;
    [[nodiscard]] std::uint32_t PositionFrames(PlayerId player) const;
    void SetVolume(PlayerId player, std::int16_t millibel);
    void SetStereoVolume(PlayerId player, float left, float right);
    void SetMute(PlayerId player, bool mute);
    void SetStereoPosition(PlayerId player, std::int16_t permille);

    [[nodiscard]] std::vector<OpenSlesConsumedBuffer> MixAdditiveStereoPcm16(
        std::span<std::int16_t> output, std::uint32_t output_rate);

private:
    struct Buffer final {
        std::uint64_t sequence{};
        std::vector<std::byte> pcm;
    };
    struct Player final {
        OpenSlesPcmFormat format;
        std::uint8_t capacity{};
        OpenSlesPlayState state{OpenSlesPlayState::stopped};
        std::int16_t millibel{};
        std::int16_t stereo_position{};
        float left_volume{1.0F};
        float right_volume{1.0F};
        bool mute{};
        std::uint32_t play_index{};
        std::uint64_t next_sequence{1U};
        double frame_position{};
        double played_source_frames{};
        std::vector<Buffer> queue;
    };

    [[nodiscard]] static std::size_t BytesPerFrame(OpenSlesPcmFormat format);
    [[nodiscard]] static std::int16_t Sample(const Player& player,
                                             const Buffer& buffer,
                                             std::size_t frame,
                                             std::size_t channel);
    [[nodiscard]] Player& Require(PlayerId player);
    [[nodiscard]] const Player& Require(PlayerId player) const;
    [[nodiscard]] static std::size_t QueuedBytes(const Player& player);

    mutable std::mutex mutex_;
    std::condition_variable queue_changed_;
    std::map<PlayerId, Player> players_;
    PlayerId next_player_{1U};
    std::vector<std::int64_t> scratch_;
    std::size_t blocking_writers_{};
    bool interrupted_{};
};

}  // namespace ogplay::audio
