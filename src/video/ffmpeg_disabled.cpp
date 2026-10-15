// Built when this configuration links no ogplay_ffmpeg package (ADR-0137):
// every entry point fails with the configure-time reason.

#include "ogplay/video/ffmpeg_video_player.h"

namespace ogplay::video {

bool FfmpegAvailable() { return false; }

std::string FfmpegUnavailableReason() { return OGPLAY_FFMPEG_MISSING_REASON; }

std::unique_ptr<VideoPlayer> OpenFfmpegVideo(std::shared_ptr<const VideoDataSource>) {
    throw VideoPlayerError(std::string("ffmpeg unavailable: ") + OGPLAY_FFMPEG_MISSING_REASON);
}

std::unique_ptr<VideoPlayer> OpenFfmpegVideo(const std::filesystem::path&) {
    throw VideoPlayerError(std::string("ffmpeg unavailable: ") + OGPLAY_FFMPEG_MISSING_REASON);
}

VideoPlayerFactory MakeFfmpegVideoPlayerFactory() {
    return [](const std::filesystem::path& host_path) { return OpenFfmpegVideo(host_path); };
}

VideoSourcePlayerFactory MakeFfmpegVideoSourcePlayerFactory() {
    return [](std::shared_ptr<const VideoDataSource> source) {
        return OpenFfmpegVideo(std::move(source));
    };
}

}  // namespace ogplay::video
