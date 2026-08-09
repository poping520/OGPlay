#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "ogplay/audio/ogg_vorbis.h"

namespace {

[[nodiscard]] std::vector<std::byte> ReadFixture() {
    const auto path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                      "tests/fixtures/audio/short-vorbis.ogg";
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open Ogg Vorbis fixture");
    const std::vector<char> bytes{std::istreambuf_iterator<char>{input}, {}};
    std::vector<std::byte> result;
    result.reserve(bytes.size());
    for (const auto value : bytes) {
        result.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(value)));
    }
    return result;
}

}  // namespace

TEST_CASE("Ogg Vorbis decoder produces bounded owned PCM16") {
    const auto encoded = ReadFixture();
    const auto decoded = ogplay::audio::DecodeOggVorbis(encoded);
    CHECK(decoded.sample_rate > 0U);
    CHECK((decoded.channels == 1U || decoded.channels == 2U));
    CHECK(decoded.Frames() > 0U);
    CHECK(decoded.interleaved_samples.size() ==
          decoded.Frames() * decoded.channels);
}

TEST_CASE("Ogg Vorbis decoder rejects empty and corrupt input") {
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::audio::DecodeOggVorbis({})),
        "Ogg Vorbis input is empty or exceeds the decode limit",
        ogplay::audio::OggVorbisDecodeError);
    const std::vector corrupt{std::byte{'O'}, std::byte{'g'},
                              std::byte{'g'}, std::byte{'S'}};
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::audio::DecodeOggVorbis(corrupt)),
        ogplay::audio::OggVorbisDecodeError);
}
