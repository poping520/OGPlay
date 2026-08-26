#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "ogplay/audio/mp3.h"

namespace {

std::vector<std::byte> ReadFixture() {
    const auto path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                      "tests/fixtures/audio/short-mp3.mp3";
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    std::vector<char> chars{std::istreambuf_iterator<char>(stream), {}};
    std::vector<std::byte> result(chars.size());
    for (std::size_t index = 0; index < chars.size(); ++index) {
        result[index] = static_cast<std::byte>(chars[index]);
    }
    return result;
}

}  // namespace

TEST_CASE("MP3 decoder produces bounded owned PCM16 after an ID3 prefix") {
    const auto fixture = ReadFixture();
    std::vector<std::byte> encoded{
        std::byte{'I'}, std::byte{'D'}, std::byte{'3'}, std::byte{4},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{0}, std::byte{4}, std::byte{'t'}, std::byte{'e'},
        std::byte{'s'}, std::byte{'t'}};
    encoded.insert(encoded.end(), fixture.begin(), fixture.end());
    const auto decoded = ogplay::audio::DecodeMp3(encoded);
    CHECK(decoded.sample_rate > 0U);
    CHECK((decoded.channels == 1U || decoded.channels == 2U));
    CHECK(decoded.Frames() > 0U);
    CHECK(decoded.interleaved_samples.size() ==
          decoded.Frames() * decoded.channels);
}

TEST_CASE("MP3 decoder rejects empty and corrupt input") {
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::audio::DecodeMp3({})),
        "MP3 input is empty or exceeds the decode limit",
        ogplay::audio::Mp3DecodeError);
    const std::vector corrupt{std::byte{'I'}, std::byte{'D'}, std::byte{'3'}};
    CHECK_THROWS_AS(static_cast<void>(ogplay::audio::DecodeMp3(corrupt)),
                    ogplay::audio::Mp3DecodeError);
}
