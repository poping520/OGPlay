#pragma once

#include <span>
#include <stdexcept>

#include "ogplay/audio/ogg_vorbis.h"

namespace ogplay::audio {

class WavDecodeError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] bool LooksLikeWav(std::span<const std::byte> encoded) noexcept;

struct WavPcmView final {
    std::uint32_t sample_rate{};
    std::uint8_t channels{};
    std::uint8_t bits{};
    std::span<const std::byte> pcm;
};
[[nodiscard]] WavPcmView ParseWav(std::span<const std::byte> encoded);

// Decodes a PCM8/PCM16 little-endian RIFF/WAVE file. Container metadata is
// not treated as raw PCM; compressed WAVE formats fail explicitly.
[[nodiscard]] Pcm16Audio DecodeWav(std::span<const std::byte> encoded);

}  // namespace ogplay::audio
