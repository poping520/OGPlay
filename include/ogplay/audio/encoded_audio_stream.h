#pragma once

#include <array>
#include <memory>
#include <stop_token>
#include "ogplay/audio/encoded_audio.h"

namespace ogplay::audio {

// Owns the immutable encoded window, never a complete decoded song. A decoder
// is confined to its player's lock; independent players have independent cursors.
class EncodedAudioStream final {
public:
    using Bytes = std::shared_ptr<const std::vector<std::byte>>;
    explicit EncodedAudioStream(Bytes bytes, std::stop_token stop = {});
    ~EncodedAudioStream();
    EncodedAudioStream(const EncodedAudioStream&) = delete;
    EncodedAudioStream& operator=(const EncodedAudioStream&) = delete;
    [[nodiscard]] std::uint32_t Rate() const;
    [[nodiscard]] std::uint8_t Channels() const;
    [[nodiscard]] std::size_t Frames() const;
    [[nodiscard]] std::array<std::int16_t, 2> Sample(std::size_t frame);
    [[nodiscard]] std::size_t BufferedPcmBytes() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace ogplay::audio
