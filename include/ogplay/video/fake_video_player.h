#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "ogplay/video/video_player.h"

namespace ogplay::video {

// Deterministic synthetic backend for behaviour tests: fixed-rate solid-color
// frames whose RGBA encodes the frame index, and ramp PCM derived from the
// audio cursor. No I/O, no threads.
class FakeVideoPlayer final : public VideoPlayer {
public:
    FakeVideoPlayer(VideoMetadata metadata, std::uint32_t frames_per_second);

    [[nodiscard]] const VideoMetadata& Metadata() const noexcept override;
    [[nodiscard]] std::optional<VideoFrame> TakeFrame(
        std::int64_t position_ms) override;
    [[nodiscard]] std::size_t ReadPcm(
        std::span<std::int16_t> interleaved) override;
    void SeekTo(std::int64_t position_ms) override;

    [[nodiscard]] std::int64_t TotalFrames() const noexcept;
    // Deterministic fill colour of a frame, exposed so tests and end-to-end
    // assertions can predict pixel content.
    [[nodiscard]] static std::uint32_t FrameColorRgba(
        std::int64_t frame_index) noexcept;

private:
    VideoMetadata metadata_;
    std::uint32_t frames_per_second_;
    std::int64_t next_frame_index_{0};
    std::int64_t last_position_ms_{-1};
    std::int64_t pcm_cursor_frames_{0};
};

}  // namespace ogplay::video
