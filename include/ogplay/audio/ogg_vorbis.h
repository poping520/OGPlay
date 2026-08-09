#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace ogplay::audio {

struct Pcm16Audio final {
    std::uint32_t sample_rate{};
    std::uint8_t channels{};
    std::vector<std::int16_t> interleaved_samples;

    [[nodiscard]] std::size_t Frames() const noexcept {
        return channels == 0U ? 0U : interleaved_samples.size() / channels;
    }
};

class OggVorbisDecodeError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] Pcm16Audio DecodeOggVorbis(
    std::span<const std::byte> encoded);

}  // namespace ogplay::audio
