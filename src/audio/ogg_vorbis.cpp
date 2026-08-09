#include "ogplay/audio/ogg_vorbis.h"

#include <limits>
#include <memory>
#include <string>

#define STB_VORBIS_HEADER_ONLY
#include "../../third_party/stb/stb_vorbis.c"

namespace ogplay::audio {
namespace {

constexpr std::size_t kMaximumEncodedBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumDecodedBytes = 128U * 1024U * 1024U;

struct VorbisCloser final {
    void operator()(stb_vorbis* decoder) const noexcept {
        stb_vorbis_close(decoder);
    }
};

}  // namespace

Pcm16Audio DecodeOggVorbis(const std::span<const std::byte> encoded) {
    if (encoded.empty() || encoded.size() > kMaximumEncodedBytes ||
        encoded.size() > static_cast<std::size_t>(
                             std::numeric_limits<int>::max())) {
        throw OggVorbisDecodeError(
            "Ogg Vorbis input is empty or exceeds the decode limit");
    }
    int error{};
    std::unique_ptr<stb_vorbis, VorbisCloser> decoder{
        stb_vorbis_open_memory(
            reinterpret_cast<const unsigned char*>(encoded.data()),
            static_cast<int>(encoded.size()), &error, nullptr)};
    if (!decoder) {
        throw OggVorbisDecodeError(
            "Ogg Vorbis stream is invalid: " + std::to_string(error));
    }
    const auto info = stb_vorbis_get_info(decoder.get());
    if ((info.channels != 1 && info.channels != 2) || info.sample_rate == 0U) {
        throw OggVorbisDecodeError(
            "Ogg Vorbis stream has an unsupported PCM format");
    }
    const auto frames = static_cast<std::size_t>(
        stb_vorbis_stream_length_in_samples(decoder.get()));
    const auto channels = static_cast<std::size_t>(info.channels);
    if (frames == 0U || frames > kMaximumDecodedBytes / sizeof(std::int16_t) /
                                      channels) {
        throw OggVorbisDecodeError(
            "Ogg Vorbis decoded PCM is empty or exceeds the decode limit");
    }
    const auto samples = frames * channels;
    if (samples > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw OggVorbisDecodeError("Ogg Vorbis sample count exceeds decoder limits");
    }
    Pcm16Audio result{
        info.sample_rate, static_cast<std::uint8_t>(info.channels),
        std::vector<std::int16_t>(samples)};
    const auto decoded_frames = stb_vorbis_get_samples_short_interleaved(
        decoder.get(), info.channels, result.interleaved_samples.data(),
        static_cast<int>(samples));
    if (decoded_frames <= 0) {
        throw OggVorbisDecodeError("Ogg Vorbis stream contains no audio frames");
    }
    const auto decoded_samples =
        static_cast<std::size_t>(decoded_frames) * channels;
    result.interleaved_samples.resize(decoded_samples);
    return result;
}

}  // namespace ogplay::audio
