#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace ogplay::video {

// Bounded facts about an opened stream; must pass ValidateVideoMetadata
// before a player is handed to callers.
struct VideoMetadata final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::int64_t duration_ms{};
    std::uint32_t audio_sample_rate{};  // 0 when the stream has no audio
    std::uint8_t audio_channels{};      // 0 when the stream has no audio

    [[nodiscard]] bool HasAudio() const noexcept {
        return audio_channels != 0U;
    }
};

struct VideoFrame final {
    std::int64_t position_ms{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> rgba8;
};

class VideoPlayerError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Throws VideoPlayerError when the metadata violates module bounds.
void ValidateVideoMetadata(const VideoMetadata& metadata);

// Pull-model decoded playback. The caller owns the position clock and polls;
// implementations never call back into the caller and keep any decode threads
// internal. Completion is a caller-side fact: position >= duration_ms.
class VideoPlayer {
public:
    VideoPlayer() = default;
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;
    virtual ~VideoPlayer() = default;

    [[nodiscard]] virtual const VideoMetadata& Metadata() const noexcept = 0;

    // Newest frame whose presentation time is <= position_ms and that has not
    // been delivered yet; nullopt while the previously delivered frame stays
    // current. position_ms must be >= 0 and must not go backwards except via
    // SeekTo; violations throw VideoPlayerError.
    [[nodiscard]] virtual std::optional<VideoFrame> TakeFrame(
        std::int64_t position_ms) = 0;

    // Fills interleaved S16 PCM from the internal audio cursor and returns
    // sample frames written; 0 at end of audio or when the stream has no
    // audio. The span size must be a multiple of the channel count.
    [[nodiscard]] virtual std::size_t ReadPcm(
        std::span<std::int16_t> interleaved) = 0;

    // Moves both frame and audio cursors; position_ms must be within
    // [0, duration_ms], violations throw VideoPlayerError.
    virtual void SeekTo(std::int64_t position_ms) = 0;
};

// Opens host_path or throws VideoPlayerError with a diagnosable reason.
using VideoPlayerFactory = std::function<std::unique_ptr<VideoPlayer>(
    const std::filesystem::path& host_path)>;

}  // namespace ogplay::video
