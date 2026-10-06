#pragma once

#include "ogplay/video/video_player.h"

namespace ogplay::video {

// Owns a decode worker with bounded read-ahead. Pulls never perform decode
// work or wait for it; an empty PCM read can also mean temporary underrun.
// Seek synchronously retires the old worker and primes the new generation.
[[nodiscard]] std::unique_ptr<VideoPlayer> MakeBufferedVideoPlayer(
    std::unique_ptr<VideoPlayer> decoder);

}  // namespace ogplay::video
