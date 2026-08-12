#include "ogplay/video/video_player.h"

namespace ogplay::video {
namespace {

constexpr std::uint32_t kMaxDimension = 4096U;
constexpr std::int64_t kMaxDurationMs = 4LL * 60LL * 60LL * 1000LL;
constexpr std::uint32_t kMinSampleRate = 4000U;
constexpr std::uint32_t kMaxSampleRate = 192000U;

}  // namespace

void ValidateVideoMetadata(const VideoMetadata& metadata) {
    if (metadata.width == 0U || metadata.height == 0U ||
        metadata.width > kMaxDimension || metadata.height > kMaxDimension) {
        throw VideoPlayerError("video metadata has out-of-bounds dimensions");
    }
    if (metadata.duration_ms <= 0 || metadata.duration_ms > kMaxDurationMs) {
        throw VideoPlayerError("video metadata has out-of-bounds duration");
    }
    const bool has_channels = metadata.audio_channels != 0U;
    const bool has_rate = metadata.audio_sample_rate != 0U;
    if (has_channels != has_rate) {
        throw VideoPlayerError(
            "video metadata audio channels and sample rate disagree");
    }
    if (has_channels && metadata.audio_channels > 2U) {
        throw VideoPlayerError("video metadata has unsupported channel count");
    }
    if (has_rate && (metadata.audio_sample_rate < kMinSampleRate ||
                     metadata.audio_sample_rate > kMaxSampleRate)) {
        throw VideoPlayerError("video metadata has unsupported sample rate");
    }
}

}  // namespace ogplay::video
