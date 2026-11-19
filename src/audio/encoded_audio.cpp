#include "ogplay/audio/encoded_audio.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "ogplay/audio/encoded_audio_stream.h"
#include "ogplay/audio/mp3.h"
#include "ogplay/audio/wav.h"

namespace ogplay::audio {
namespace {

[[nodiscard]] bool LooksLikeOgg(const std::span<const std::byte> encoded) {
    return encoded.size() >= 4U && encoded[0] == std::byte{'O'} &&
           encoded[1] == std::byte{'g'} && encoded[2] == std::byte{'g'} &&
           encoded[3] == std::byte{'S'};
}

class SpanAudioSource final : public EncodedAudioDataSource {
public:
    explicit SpanAudioSource(std::span<const std::byte> bytes) : bytes_(bytes) {}

    std::uint64_t Size() const noexcept override { return bytes_.size(); }

    std::size_t ReadAt(std::uint64_t offset, std::span<std::byte> output, std::stop_token stop) const override {
        if (stop.stop_requested() || offset >= bytes_.size()) return 0;
        const auto remaining = bytes_.subspan(static_cast<std::size_t>(offset));
        const auto count = std::min(remaining.size(), output.size());
        std::ranges::copy(remaining.first(count), output.begin());
        return count;
    }

private:
    std::span<const std::byte> bytes_;
};

}  // namespace

AudioDecodeError::AudioDecodeError(std::string_view container, std::string_view codec, std::string_view detail)
    : std::runtime_error((container.empty() ? "unrecognized audio container"
                                            : "unsupported audio: " + std::string(container) + "/" +
                                                  std::string(codec.empty() ? "unknown" : codec)) +
                         " (" + std::string(detail) + ")"),
      codec_(codec) {}

bool LooksLikeMp4(std::span<const std::byte> encoded) {
    return encoded.size() >= 8 && encoded[4] == std::byte{'f'} && encoded[5] == std::byte{'t'} &&
           encoded[6] == std::byte{'y'} && encoded[7] == std::byte{'p'};
}

bool LooksLikeMp3(std::span<const std::byte> encoded) {
    return encoded.size() >= 3 &&
           ((encoded[0] == std::byte{'I'} && encoded[1] == std::byte{'D'} && encoded[2] == std::byte{'3'}) ||
            (encoded[0] == std::byte{0xff} && (encoded[1] & std::byte{0xe6}) == std::byte{0xe2}));
}

std::unique_ptr<ExternalAudioStream> OpenExternalAudio(const ExternalAudioDecoder& decoder,
                                                       std::shared_ptr<const EncodedAudioDataSource> source,
                                                       std::stop_token stop) {
    if (!decoder) throw AudioDecodeError("mp4", {}, "external decoder unavailable");
    auto stream = decoder(std::move(source), stop);
    if (!stream || stream->Rate() < 4000 || stream->Rate() > 192000 || !stream->Frames() ||
        (stream->Channels() != 1 && stream->Channels() != 2)) {
        throw AudioDecodeError("mp4", {}, "invalid audio stream metadata");
    }
    return stream;
}

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

Pcm16Audio DecodeEncodedAudio(const std::span<const std::byte> encoded, const ExternalAudioDecoder& decoder) {
    if (encoded.empty() || encoded.size() > kMaximumEncodedAudioBytes) {
        throw std::invalid_argument(
            "encoded audio is empty or exceeds the decode limit");
    }
    if (LooksLikeMp4(encoded)) {
        auto stream = OpenExternalAudio(decoder, std::make_shared<SpanAudioSource>(encoded));
        Pcm16Audio result;
        result.sample_rate = stream->Rate();
        result.channels = stream->Channels();
        const auto maximum_frames = kMaximumDecodedPcmBytes / sizeof(std::int16_t) / result.channels;
        if (stream->Frames() > maximum_frames) throw AudioDecodeError("mp4", {}, "decoded PCM exceeds the byte budget");
        std::array<std::int16_t, kAudioPcmBlockSamples> block{};
        while (result.Frames() < stream->Frames()) {
            const auto frames = std::min(block.size() / result.channels, stream->Frames() - result.Frames());
            const auto samples = std::span(block).first(frames * result.channels);
            const auto read = stream->ReadPcm(samples, {});
            if (read == 0 || read > frames) throw AudioDecodeError("mp4", {}, "truncated decoded PCM");
            const auto decoded = samples.first(read * result.channels);
            result.interleaved_samples.insert(result.interleaved_samples.end(), decoded.begin(), decoded.end());
        }
        return result;
    }
    const bool ogg = LooksLikeOgg(encoded);
    const bool wav = LooksLikeWav(encoded);
    try {
        if (ogg) return DecodeOggVorbis(encoded);
        if (wav) return DecodeWav(encoded);
        return DecodeMp3(encoded);
    } catch (const std::runtime_error& error) {
        if (ogg) throw AudioDecodeError("ogg", "vorbis", error.what());
        if (wav) throw AudioDecodeError("wav", "pcm", error.what());
        if (LooksLikeMp3(encoded)) throw AudioDecodeError("mp3", "mp3", error.what());
        throw AudioDecodeError({}, {}, error.what());
    }
}

}  // namespace ogplay::audio
