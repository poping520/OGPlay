#pragma once

#include <cstddef>
#include <span>

namespace ogplay::audio {

struct MusicPlaybackRequest final {
    std::span<const std::byte> encoded;
    bool loop{};
};

class MusicPlayer {
public:
    virtual ~MusicPlayer() = default;
    virtual void Play(const MusicPlaybackRequest& request) = 0;
};

}  // namespace ogplay::audio
