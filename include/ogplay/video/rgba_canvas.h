#pragma once

#include <cstdint>
#include <vector>

#include "ogplay/video/video_player.h"

namespace ogplay::video {

// Deterministic nearest-neighbour composition of a decoded frame onto an
// opaque black canvas: aspect-preserving scale, centered (letterbox or
// pillarbox). Canvas dimensions follow the same bounds as VideoMetadata.
[[nodiscard]] std::vector<std::uint8_t> ComposeRgbaOnCanvas(
    const VideoFrame& frame, std::uint32_t canvas_width,
    std::uint32_t canvas_height);

}  // namespace ogplay::video
