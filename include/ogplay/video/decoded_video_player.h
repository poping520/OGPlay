#pragma once

#include "ogplay/video/video_player.h"

namespace ogplay::video {

// An owned decoded picture, before presentation conversion. The decoder and
// its pictures are used serially by their owner (the buffered decode worker).
// StorageBytes accounts for retained pixel backing, including plane padding;
// conversion is one-shot and may throw. No borrowed codec frame may escape.
class DecodedVideoPicture {
public:
    virtual ~DecodedVideoPicture() = default;
    [[nodiscard]] virtual std::int64_t PositionMs() const noexcept = 0;
    [[nodiscard]] virtual std::size_t StorageBytes() const noexcept = 0;
    [[nodiscard]] virtual VideoFrame ToRgba() = 0;
};

// Optional staged decoding interface. Ordinary VideoPlayer implementations
// continue to work through the buffered wrapper's owned RGBA adapter.
class DecodedVideoPlayer : public VideoPlayer {
public:
    [[nodiscard]] virtual std::unique_ptr<DecodedVideoPicture> TakeDecodedPicture(
        std::int64_t position_ms) = 0;
};

} // namespace ogplay::video
