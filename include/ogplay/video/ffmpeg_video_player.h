#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "ogplay/video/video_player.h"

namespace ogplay::video {

// True when this build links the ogplay_ffmpeg SDK and its startup self-check
// passed: runtime and header versions agree and the mov demuxer plus the
// h264/mpeg4/aac decoders are present (ADR-0136). Stable per process.
[[nodiscard]] bool FfmpegAvailable();

// Human-readable reason when FfmpegAvailable() is false; empty otherwise.
[[nodiscard]] std::string FfmpegUnavailableReason();

// Opens a decoded playback session; throws VideoPlayerError when FFmpeg is
// unavailable, the input cannot be demuxed or has no video stream, and
// VideoCodecUnsupportedError when a video or audio codec is not built in.
// The host-path overload reads the file through the same custom AVIO as
// VideoDataSource; the SDK carries no FFmpeg protocols.
[[nodiscard]] std::unique_ptr<VideoPlayer> OpenFfmpegVideo(
    const std::filesystem::path& host_path);

[[nodiscard]] VideoPlayerFactory MakeFfmpegVideoPlayerFactory();
[[nodiscard]] std::unique_ptr<VideoPlayer> OpenFfmpegVideo(
    std::shared_ptr<const VideoDataSource> source);
[[nodiscard]] VideoSourcePlayerFactory MakeFfmpegVideoSourcePlayerFactory();

}  // namespace ogplay::video
