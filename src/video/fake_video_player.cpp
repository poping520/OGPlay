#include "ogplay/video/fake_video_player.h"

#include <algorithm>
#include <cstring>

namespace ogplay::video {
namespace {

constexpr std::uint32_t kMinFps = 1U;
constexpr std::uint32_t kMaxFps = 120U;

}  // namespace

FakeVideoPlayer::FakeVideoPlayer(VideoMetadata metadata,
                                 std::uint32_t frames_per_second)
    : metadata_(metadata), frames_per_second_(frames_per_second) {
    ValidateVideoMetadata(metadata_);
    if (frames_per_second_ < kMinFps || frames_per_second_ > kMaxFps) {
        throw VideoPlayerError("fake video player fps out of bounds");
    }
}

const VideoMetadata& FakeVideoPlayer::Metadata() const noexcept {
    return metadata_;
}

std::int64_t FakeVideoPlayer::TotalFrames() const noexcept {
    const std::int64_t frames =
        (metadata_.duration_ms * frames_per_second_ + 999) / 1000;
    return std::max<std::int64_t>(frames, 1);
}

std::uint32_t FakeVideoPlayer::FrameColorRgba(
    std::int64_t frame_index) noexcept {
    const auto index = static_cast<std::uint64_t>(frame_index);
    const auto r = static_cast<std::uint32_t>((index * 7U) & 0xFFU);
    const auto g = static_cast<std::uint32_t>((index * 13U) & 0xFFU);
    const auto b = static_cast<std::uint32_t>((index * 31U) & 0xFFU);
    return (r << 24U) | (g << 16U) | (b << 8U) | 0xFFU;
}

std::optional<VideoFrame> FakeVideoPlayer::TakeFrame(
    std::int64_t position_ms) {
    if (position_ms < 0) {
        throw VideoPlayerError("fake video player position must be >= 0");
    }
    if (position_ms < last_position_ms_) {
        throw VideoPlayerError(
            "fake video player position went backwards without SeekTo");
    }
    last_position_ms_ = position_ms;

    const std::int64_t due_index =
        std::min(position_ms * frames_per_second_ / 1000, TotalFrames() - 1);
    if (due_index < next_frame_index_) return std::nullopt;
    next_frame_index_ = due_index + 1;

    VideoFrame frame;
    frame.position_ms = due_index * 1000 / frames_per_second_;
    frame.width = metadata_.width;
    frame.height = metadata_.height;
    const std::uint32_t color = FrameColorRgba(due_index);
    const std::uint8_t rgba[4] = {
        static_cast<std::uint8_t>(color >> 24U),
        static_cast<std::uint8_t>(color >> 16U),
        static_cast<std::uint8_t>(color >> 8U),
        static_cast<std::uint8_t>(color),
    };
    frame.rgba8.resize(static_cast<std::size_t>(frame.width) * frame.height *
                       4U);
    for (std::size_t offset = 0; offset < frame.rgba8.size(); offset += 4U) {
        std::memcpy(frame.rgba8.data() + offset, rgba, 4U);
    }
    return frame;
}

std::size_t FakeVideoPlayer::ReadPcm(std::span<std::int16_t> interleaved) {
    if (!metadata_.HasAudio()) return 0U;
    const auto channels = static_cast<std::size_t>(metadata_.audio_channels);
    if (interleaved.size() % channels != 0U) {
        throw VideoPlayerError(
            "fake video player pcm span not a multiple of channel count");
    }
    const std::int64_t total_frames =
        metadata_.duration_ms * metadata_.audio_sample_rate / 1000;
    const std::int64_t remaining =
        std::max<std::int64_t>(total_frames - pcm_cursor_frames_, 0);
    const std::size_t frames = std::min<std::size_t>(
        interleaved.size() / channels, static_cast<std::size_t>(remaining));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto sample = static_cast<std::int16_t>(
            (pcm_cursor_frames_ + static_cast<std::int64_t>(frame)) & 0x7FFF);
        for (std::size_t channel = 0; channel < channels; ++channel) {
            interleaved[frame * channels + channel] = sample;
        }
    }
    pcm_cursor_frames_ += static_cast<std::int64_t>(frames);
    return frames;
}

void FakeVideoPlayer::SeekTo(std::int64_t position_ms) {
    if (position_ms < 0 || position_ms > metadata_.duration_ms) {
        throw VideoPlayerError("fake video player seek out of bounds");
    }
    next_frame_index_ = position_ms * frames_per_second_ / 1000;
    last_position_ms_ = position_ms;
    if (metadata_.HasAudio()) {
        pcm_cursor_frames_ = position_ms * metadata_.audio_sample_rate / 1000;
    }
}

}  // namespace ogplay::video
