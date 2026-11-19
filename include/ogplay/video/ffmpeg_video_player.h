#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "ogplay/audio/encoded_audio.h"
#include "ogplay/video/video_player.h"

namespace ogplay::video {

// True when the runtime version matches its headers and the required audio
// and video components are present (ADR-0136). Stable per process.
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

// Accepts MP3 and audio-only ISO BMFF (attached artwork is allowed).
// Opening a stream reports a failed SDK self-check as AudioDecodeError.
[[nodiscard]] audio::ExternalAudioDecoder MakeFfmpegAudioDecoder();

}  // namespace ogplay::video
