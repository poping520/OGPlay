#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "ogplay/audio/encoded_music.h"
#include "ogplay/audio/pcm_mix.h"

namespace {

[[nodiscard]] std::vector<std::byte> ReadSound() {
    const auto path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                      "tests/fixtures/audio/short-vorbis.ogg";
    std::ifstream input(path, std::ios::binary);
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

TEST_CASE("encoded music players keep independent seek duration and mix") {
    const auto encoded = ReadSound();
    ogplay::audio::EncodedMusicMixer mixer;
    const auto first = mixer.Create();
    const auto second = mixer.Create();
    REQUIRE(mixer.SetEncoded(first, encoded));
    REQUIRE(mixer.SetEncoded(second, encoded));
    REQUIRE(mixer.Prepare(first));
    REQUIRE(mixer.Prepare(second));
    CHECK(mixer.DurationMs(first) > 0);
    mixer.SetVolume(first, 1.0F, 0.0F);
    mixer.SetVolume(second, 0.0F, 1.0F);
    mixer.SetLooping(second, true);
    mixer.Start(first);
    mixer.Start(second);
    mixer.SeekMs(first, mixer.DurationMs(first));
    std::vector<std::int16_t> pcm(128U * 2U);
    std::vector<std::int64_t> accumulator(pcm.size());
    mixer.MixIntoAccumulator(accumulator, 48000U);
    ogplay::audio::SaturateStereoPcm16(accumulator, pcm);
    CHECK(std::ranges::any_of(pcm, [](const std::int16_t sample) {
        return sample != 0;
    }));
    mixer.Reset(first);
    CHECK_FALSE(mixer.IsPlaying(first));
    CHECK(mixer.IsPlaying(second));
    mixer.Destroy(first);
    mixer.Destroy(second);
}

TEST_CASE("encoded music restarts from zero after natural completion") {
    ogplay::audio::EncodedMusicMixer mixer;
    const auto player = mixer.Create();
    REQUIRE(mixer.SetEncoded(player, ReadSound()));
    REQUIRE(mixer.Prepare(player));
    mixer.Start(player);
    std::vector<std::int64_t> drain(48000U * 2U);
    mixer.MixIntoAccumulator(drain, 48000U);
    REQUIRE(mixer.Completed(player));
    mixer.Start(player);
    std::vector<std::int64_t> replay(64U * 2U);
    mixer.MixIntoAccumulator(replay, 48000U);
    CHECK(std::ranges::any_of(replay, [](const auto sample) {
        return sample != 0;
    }));
}
