#include "ogplay/audio/mp3.h"

#include <array>
#include <limits>
#include <string>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define MINIMP3_IMPLEMENTATION
#include "../../third_party/minimp3/minimp3.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace ogplay::audio {
namespace {

constexpr std::size_t kMaximumEncodedBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumDecodedBytes = 128U * 1024U * 1024U;

}  // namespace

Pcm16Audio DecodeMp3(const std::span<const std::byte> encoded) {
    if (encoded.empty() || encoded.size() > kMaximumEncodedBytes ||
        encoded.size() >
            static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw Mp3DecodeError("MP3 input is empty or exceeds the decode limit");
    }

    mp3dec_t decoder{};
    mp3dec_init(&decoder);
    Pcm16Audio result;
    std::size_t cursor{};
    std::array<mp3d_sample_t, MINIMP3_MAX_SAMPLES_PER_FRAME> frame{};
    while (cursor < encoded.size()) {
        const auto remaining = encoded.size() - cursor;
        mp3dec_frame_info_t info{};
        const auto samples_per_channel = mp3dec_decode_frame(
            &decoder,
            reinterpret_cast<const std::uint8_t*>(encoded.data() + cursor),
            static_cast<int>(remaining), frame.data(), &info);
        if (info.frame_bytes <= 0 ||
            static_cast<std::size_t>(info.frame_bytes) > remaining) {
            break;
        }
        cursor += static_cast<std::size_t>(info.frame_bytes);
        if (samples_per_channel == 0) continue;
        if ((info.channels != 1 && info.channels != 2) || info.hz <= 0) {
            throw Mp3DecodeError("MP3 stream has an unsupported PCM format");
        }
        if (result.sample_rate == 0U) {
            result.sample_rate = static_cast<std::uint32_t>(info.hz);
            result.channels = static_cast<std::uint8_t>(info.channels);
        } else if (result.sample_rate != static_cast<std::uint32_t>(info.hz) ||
                   result.channels != static_cast<std::uint8_t>(info.channels)) {
            throw Mp3DecodeError("MP3 stream changes PCM format between frames");
        }
        const auto sample_count =
            static_cast<std::size_t>(samples_per_channel) * result.channels;
        if (sample_count > frame.size() ||
            result.interleaved_samples.size() >
                kMaximumDecodedBytes / sizeof(std::int16_t) - sample_count) {
            throw Mp3DecodeError("MP3 decoded PCM exceeds the decode limit");
        }
        result.interleaved_samples.insert(result.interleaved_samples.end(),
                                          frame.begin(),
                                          frame.begin() + sample_count);
    }
    if (result.interleaved_samples.empty()) {
        throw Mp3DecodeError("MP3 stream contains no decodable audio frames");
    }
    return result;
}

}  // namespace ogplay::audio
