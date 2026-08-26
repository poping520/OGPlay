#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>

#include "ogplay/audio/ogg_vorbis.h"

namespace ogplay::audio {

class Mp3DecodeError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] Pcm16Audio DecodeMp3(std::span<const std::byte> encoded);

}  // namespace ogplay::audio
