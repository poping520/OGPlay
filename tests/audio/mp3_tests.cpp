#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "ogplay/audio/encoded_audio_stream.h"
#include "ogplay/video/ffmpeg_video_player.h"

namespace {

std::vector<std::byte> ReadFixture(const char* name = "short-cbr-mp3.mp3") {
    const auto path = std::filesystem::path{OGPLAY_SOURCE_DIR} / "tests/fixtures/audio" / name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    const std::vector<char> chars{std::istreambuf_iterator<char>(stream), {}};
    const auto bytes = std::as_bytes(std::span(chars));
    return {bytes.begin(), bytes.end()};
}

}  // namespace

TEST_CASE("FFmpeg decodes MP3 with ID3 tags and applies gapless metadata") {
    const auto fixture = ReadFixture();
    const auto decoder = ogplay::video::MakeFfmpegAudioDecoder();
    const auto reference = ogplay::audio::DecodeEncodedAudio(fixture, decoder);
    CHECK(reference.sample_rate == 16000);
    CHECK(reference.channels == 1);
    // 0.3 seconds after trimming the LAME encoder delay and padding.
    CHECK(reference.Frames() == 4800);
    CHECK(std::ranges::any_of(reference.interleaved_samples, [](auto sample) { return sample != 0; }));

    std::vector<std::byte> tagged;
    SUBCASE("ID3v1 trailer") {
        tagged = fixture;
        SUBCASE("tag directly after MPEG frames") {}
        SUBCASE("padding before tag") {
            tagged.insert(tagged.end(), {std::byte{0xff}, std::byte{3}, std::byte{3}, std::byte{0}, std::byte{0},
                                         std::byte{0}, std::byte{1}});
        }
        const auto tag_start = tagged.size();
        tagged.resize(tag_start + 128);
        tagged[tag_start] = std::byte{'T'};
        tagged[tag_start + 1] = std::byte{'A'};
        tagged[tag_start + 2] = std::byte{'G'};
        tagged[tag_start + 127] = std::byte{255};  // Unspecified genre.
    }
    SUBCASE("ID3v2 prefix") {
        tagged = {std::byte{'I'}, std::byte{'D'}, std::byte{'3'}, std::byte{4},   std::byte{0},   std::byte{0},
                  std::byte{0},   std::byte{0},   std::byte{0},   std::byte{13},  std::byte{'T'}, std::byte{'I'},
                  std::byte{'T'}, std::byte{'2'}, std::byte{0},   std::byte{0},   std::byte{0},   std::byte{3},
                  std::byte{0},   std::byte{0},   std::byte{0},   std::byte{'A'}, std::byte{'B'}};
        tagged.insert(tagged.end(), fixture.begin(), fixture.end());
    }
    SUBCASE("MPEG sync after a short junk prefix") {
        // The Info tag is in the first MPEG frame. Use the audio frames alone
        // when testing recovery from leading junk.
        tagged.resize(32);
        tagged.insert(tagged.end(), fixture.begin() + 180, fixture.end());
        const auto decoded = ogplay::audio::DecodeEncodedAudio(tagged, decoder);
        CHECK(decoded.Frames() == 11 * 576);
        CHECK(std::ranges::any_of(decoded.interleaved_samples, [](auto sample) { return sample != 0; }));
        return;
    }
    const auto decoded = ogplay::audio::DecodeEncodedAudio(tagged, decoder);
    CHECK(decoded.sample_rate == reference.sample_rate);
    CHECK(decoded.channels == reference.channels);
    CHECK(decoded.interleaved_samples == reference.interleaved_samples);
}

TEST_CASE("FFmpeg MP3 stream rewinds to the first gapless PCM samples") {
    const auto bytes = std::make_shared<const std::vector<std::byte>>(ReadFixture());
    const auto decoder = ogplay::video::MakeFfmpegAudioDecoder();
    const auto reference = ogplay::audio::DecodeEncodedAudio(*bytes, decoder);
    ogplay::audio::EncodedAudioStream stream(bytes, {}, decoder);
    REQUIRE(stream.Frames() == reference.Frames());
    REQUIRE(stream.Sample(stream.Frames() - 1)[0] == reference.interleaved_samples.back());
    for (std::size_t frame = 0; frame < 256; ++frame) {
        REQUIRE(stream.Sample(frame)[0] == reference.interleaved_samples[frame]);
    }
}

TEST_CASE("FFmpeg rejects free-format MP3 with its container and codec names") {
    const auto decoder = ogplay::video::MakeFfmpegAudioDecoder();
    const auto fixture = ReadFixture("short-mp3.mp3");
    try {
        static_cast<void>(ogplay::audio::DecodeEncodedAudio(fixture, decoder));
        FAIL("free-format MP3 must fail");
    } catch (const ogplay::audio::AudioDecodeError& error) {
        const auto failure = error.Failure();
        CHECK(failure.message.starts_with("unsupported audio: mp3/mp3 ("));
        CHECK(failure.codec == "mp3");
    }
}
