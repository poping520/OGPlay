#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "ogplay/video/video_player.h"

namespace ogplay::video {

// True when the pinned FFmpeg 7 shared libraries are loadable in this
// process (see ADR-0021). The probe result is stable per process.
[[nodiscard]] bool FfmpegAvailable();

// Human-readable reason when FfmpegAvailable() is false; empty otherwise.
[[nodiscard]] std::string FfmpegUnavailableReason();

// Opens a decoded playback session; throws VideoPlayerError when FFmpeg is
// unavailable, the file cannot be demuxed, or it has no video stream.
[[nodiscard]] std::unique_ptr<VideoPlayer> OpenFfmpegVideo(
    const std::filesystem::path& host_path);

[[nodiscard]] VideoPlayerFactory MakeFfmpegVideoPlayerFactory();

}  // namespace ogplay::video
