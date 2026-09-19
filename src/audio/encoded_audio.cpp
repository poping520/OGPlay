#include "ogplay/audio/encoded_audio.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "ogplay/audio/mp3.h"
#include "ogplay/audio/wav.h"

namespace ogplay::audio {
namespace {

[[nodiscard]] bool LooksLikeOgg(const std::span<const std::byte> encoded) {
    return encoded.size() >= 4U && encoded[0] == std::byte{'O'} &&
           encoded[1] == std::byte{'g'} && encoded[2] == std::byte{'g'} &&
           encoded[3] == std::byte{'S'};
}

}  // namespace

std::vector<std::byte> SliceSourceWindow(const std::span<const std::byte> bytes,
                                         const std::uint64_t offset,
                                         const std::uint64_t length) {
    if (offset > bytes.size()) {
        throw std::invalid_argument("encoded audio window offset is past EOF");
    }
    const auto available = bytes.size() - static_cast<std::size_t>(offset);
    const bool remainder =
        length == 0U ||
        length == std::numeric_limits<std::uint64_t>::max() ||
        length == static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max());
    const auto take = remainder ? available
                                : (length > available
                                       ? std::size_t{}
                                       : static_cast<std::size_t>(length));
    if (!remainder && take == 0U) {
        throw std::invalid_argument("encoded audio window exceeds the source");
    }
    if (take > kMaximumEncodedAudioBytes) {
        throw std::length_error("encoded audio window exceeds the byte budget");
    }
    const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
    return {begin, begin + static_cast<std::ptrdiff_t>(take)};
}

Pcm16Audio DecodeEncodedAudio(const std::span<const std::byte> encoded) {
    if (encoded.empty() || encoded.size() > kMaximumEncodedAudioBytes) {
        throw std::invalid_argument(
            "encoded audio is empty or exceeds the decode limit");
    }
    if (LooksLikeOgg(encoded)) return DecodeOggVorbis(encoded);
    if (LooksLikeWav(encoded)) return DecodeWav(encoded);
    return DecodeMp3(encoded);
}

}  // namespace ogplay::audio
