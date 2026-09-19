#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ogplay/audio/wav.h"

namespace {

[[nodiscard]] std::vector<std::byte> MakePcm16Wav(
    const std::initializer_list<std::int16_t> samples,
    const std::uint32_t sample_rate, const std::uint16_t channels) {
    const auto data_bytes =
        static_cast<std::uint32_t>(samples.size() * 2U);
    std::vector<std::byte> bytes;
    const auto push32 = [&](const std::uint32_t value) {
        bytes.push_back(static_cast<std::byte>(value & 0xffU));
        bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
        bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
        bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
    };
    const auto push16 = [&](const std::uint16_t value) {
        bytes.push_back(static_cast<std::byte>(value & 0xffU));
        bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    };
    bytes.insert(bytes.end(), {std::byte{'R'}, std::byte{'I'}, std::byte{'F'},
                               std::byte{'F'}});
    push32(36U + data_bytes);
    bytes.insert(bytes.end(), {std::byte{'W'}, std::byte{'A'}, std::byte{'V'},
                               std::byte{'E'}, std::byte{'f'}, std::byte{'m'},
                               std::byte{'t'}, std::byte{' '}});
    push32(16U);
    push16(1U);
    push16(channels);
    push32(sample_rate);
    push32(sample_rate * channels * 2U);
    push16(static_cast<std::uint16_t>(channels * 2U));
    push16(16U);
    bytes.insert(bytes.end(), {std::byte{'d'}, std::byte{'a'}, std::byte{'t'},
                               std::byte{'a'}});
    push32(data_bytes);
    for (const auto sample : samples) {
        push16(static_cast<std::uint16_t>(sample));
    }
    return bytes;
}

}  // namespace

TEST_CASE("WAVE decoder publishes PCM16 without treating the container as PCM") {
    const auto encoded = MakePcm16Wav({1000, -1000, 2000, -2000}, 8000U, 2U);
    CHECK(ogplay::audio::LooksLikeWav(encoded));
    const auto decoded = ogplay::audio::DecodeWav(encoded);
    CHECK(decoded.sample_rate == 8000U);
    CHECK(decoded.channels == 2U);
    CHECK(decoded.interleaved_samples ==
          std::vector<std::int16_t>{1000, -1000, 2000, -2000});
}

TEST_CASE("WAVE decoder rejects compressed or truncated files") {
    CHECK_THROWS_AS(static_cast<void>(ogplay::audio::DecodeWav({})),
                    ogplay::audio::WavDecodeError);
    auto encoded = MakePcm16Wav({1}, 8000U, 1U);
    encoded[20] = std::byte{3};
    CHECK_THROWS_AS(static_cast<void>(ogplay::audio::DecodeWav(encoded)),
                    ogplay::audio::WavDecodeError);
}
