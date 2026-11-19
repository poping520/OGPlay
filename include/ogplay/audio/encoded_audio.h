#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/audio/ogg_vorbis.h"

namespace ogplay::audio {

constexpr std::size_t kMaximumEncodedAudioBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumDecodedPcmBytes = 128U * 1024U * 1024U;
constexpr std::size_t kAudioPcmBlockSamples = 8192U;

class EncodedAudioDataSource;

class ExternalAudioStream {
public:
    virtual ~ExternalAudioStream() = default;
    [[nodiscard]] virtual std::uint32_t Rate() const noexcept = 0;
    [[nodiscard]] virtual std::uint8_t Channels() const noexcept = 0;
    [[nodiscard]] virtual std::size_t Frames() const noexcept = 0;
    // Returns frames of interleaved S16. A short read is valid; zero means EOF.
    virtual std::size_t ReadPcm(std::span<std::int16_t> output, std::stop_token stop) = 0;
    virtual void Rewind() = 0;
};

using ExternalAudioDecoder =
    std::function<std::unique_ptr<ExternalAudioStream>(std::shared_ptr<const EncodedAudioDataSource>, std::stop_token)>;

struct AudioDecodeFailure {
    std::string message;
    std::string codec;
};

class AudioDecodeError final : public std::runtime_error {
public:
    AudioDecodeError(std::string_view container, std::string_view codec, std::string_view detail);

    [[nodiscard]] AudioDecodeFailure Failure() const { return {what(), codec_}; }

private:
    std::string codec_;
};

[[nodiscard]] bool LooksLikeMp4(std::span<const std::byte> encoded);
[[nodiscard]] bool LooksLikeMp3(std::span<const std::byte> encoded);
[[nodiscard]] std::unique_ptr<ExternalAudioStream>
OpenExternalAudio(const ExternalAudioDecoder& decoder, std::shared_ptr<const EncodedAudioDataSource> source,
                  std::stop_token stop = {});

[[nodiscard]] std::vector<std::byte> SliceSourceWindow(
    std::span<const std::byte> bytes, std::uint64_t offset,
    std::uint64_t length);

[[nodiscard]] Pcm16Audio DecodeEncodedAudio(std::span<const std::byte> encoded,
                                            const ExternalAudioDecoder& decoder = {});

}  // namespace ogplay::audio
