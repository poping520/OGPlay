#include "ogplay/audio/wav.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace ogplay::audio {
namespace {

constexpr std::size_t kMaximumEncodedBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumDecodedBytes = 128U * 1024U * 1024U;

[[nodiscard]] std::uint16_t Read16(const std::span<const std::byte> bytes,
                                   const std::size_t offset) {
    return static_cast<std::uint16_t>(
               std::to_integer<std::uint8_t>(bytes[offset])) |
           static_cast<std::uint16_t>(
               std::to_integer<std::uint8_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t Read32(const std::span<const std::byte> bytes,
                                   const std::size_t offset) {
    return static_cast<std::uint32_t>(Read16(bytes, offset)) |
           (static_cast<std::uint32_t>(Read16(bytes, offset + 2U)) << 16U);
}

[[nodiscard]] bool TagEquals(const std::span<const std::byte> bytes,
                             const std::size_t offset,
                             const std::string_view tag) {
    if (offset + tag.size() > bytes.size()) return false;
    for (std::size_t index = 0; index < tag.size(); ++index) {
        if (bytes[offset + index] != static_cast<std::byte>(tag[index])) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool LooksLikeWav(const std::span<const std::byte> encoded) noexcept {
    return encoded.size() >= 12U && TagEquals(encoded, 0U, "RIFF") &&
           TagEquals(encoded, 8U, "WAVE");
}

Pcm16Audio DecodeWav(const std::span<const std::byte> encoded) {
    if (!LooksLikeWav(encoded) || encoded.size() > kMaximumEncodedBytes) {
        throw WavDecodeError("WAVE input is not a bounded RIFF/WAVE file");
    }
    std::uint16_t format_tag{};
    std::uint16_t channels{};
    std::uint32_t sample_rate{};
    std::uint16_t bits{};
    bool have_format{};
    std::span<const std::byte> pcm;
    std::size_t cursor = 12U;
    while (cursor + 8U <= encoded.size()) {
        const auto chunk_size = Read32(encoded, cursor + 4U);
        const auto data_offset = cursor + 8U;
        if (data_offset + chunk_size > encoded.size()) {
            throw WavDecodeError("WAVE chunk exceeds the file length");
        }
        const auto payload = encoded.subspan(data_offset, chunk_size);
        if (TagEquals(encoded, cursor, "fmt ")) {
            if (chunk_size < 16U) {
                throw WavDecodeError("WAVE fmt chunk is truncated");
            }
            format_tag = Read16(payload, 0U);
            channels = Read16(payload, 2U);
            sample_rate = Read32(payload, 4U);
            bits = Read16(payload, 14U);
            have_format = true;
        } else if (TagEquals(encoded, cursor, "data")) {
            pcm = payload;
        }
        cursor = data_offset + chunk_size;
        if ((chunk_size & 1U) != 0U) ++cursor;
    }
    if (!have_format || pcm.empty()) {
        throw WavDecodeError("WAVE file is missing fmt or data");
    }
    if (format_tag != 1U || (channels != 1U && channels != 2U) ||
        sample_rate == 0U || (bits != 8U && bits != 16U)) {
        throw WavDecodeError("WAVE stream is not PCM8/16 mono/stereo");
    }
    const auto bytes_per_frame =
        static_cast<std::size_t>(channels) * (bits / 8U);
    if (pcm.size() % bytes_per_frame != 0U) {
        throw WavDecodeError("WAVE data is not frame aligned");
    }
    const auto frames = pcm.size() / bytes_per_frame;
    if (frames == 0U ||
        frames > kMaximumDecodedBytes / sizeof(std::int16_t) / channels) {
        throw WavDecodeError("WAVE PCM is empty or exceeds the decode limit");
    }
    Pcm16Audio result{sample_rate, static_cast<std::uint8_t>(channels), {}};
    result.interleaved_samples.resize(frames * channels);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const auto sample_index = frame * channels + channel;
            if (bits == 8U) {
                const auto value =
                    std::to_integer<std::uint8_t>(pcm[sample_index]);
                result.interleaved_samples[sample_index] =
                    static_cast<std::int16_t>(
                        (static_cast<std::int32_t>(value) - 128) * 256);
            } else {
                const auto offset = sample_index * 2U;
                result.interleaved_samples[sample_index] =
                    static_cast<std::int16_t>(Read16(pcm, offset));
            }
        }
    }
    return result;
}

}  // namespace ogplay::audio
