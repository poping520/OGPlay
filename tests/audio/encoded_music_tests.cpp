#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <chrono>
#include <thread>

#include "ogplay/audio/encoded_music.h"
#include "ogplay/audio/pcm_mix.h"

namespace {

[[nodiscard]] std::vector<std::byte> ReadSound(const char* name = "short-vorbis.ogg") {
    const auto path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                      "tests/fixtures/audio" / name;
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

TEST_CASE("incremental music agrees with full decoding across blocks and seeks") {
    using namespace ogplay::audio;
    for (const auto* name : {"short-vorbis.ogg", "short-mp3.mp3"}) {
        CAPTURE(name);
        auto bytes = std::make_shared<const std::vector<std::byte>>(ReadSound(name));
        const auto reference = DecodeEncodedAudio(*bytes);
        EncodedAudioStream stream(bytes);
        REQUIRE(stream.Rate() == reference.sample_rate);
        REQUIRE(stream.Channels() == reference.channels);
        REQUIRE(stream.Frames() == reference.Frames());
        bool matches = true;
        for (std::size_t frame = 0; frame < reference.Frames(); ++frame) {
            const auto sample = stream.Sample(frame);
            matches &= sample[0] == reference.interleaved_samples[frame * reference.channels];
            matches &= sample[1] == reference.interleaved_samples[
                frame * reference.channels + (reference.channels == 1 ? 0 : 1)];
        }
        CHECK(matches);
        for (auto frame : {reference.Frames() / 2, std::size_t{0}, reference.Frames() - 1}) {
            CHECK(stream.Sample(frame)[0] == reference.interleaved_samples[frame * reference.channels]);
        }
        CHECK(stream.BufferedPcmBytes() <= 16384U);
        std::stop_source cancelled;
        cancelled.request_stop();
        CHECK_THROWS(EncodedAudioStream(bytes, cancelled.get_token()));
    }
}

TEST_CASE("encoded music async owners cancel reset release and bound instances") {
    using namespace ogplay::audio;
    EncodedMusicMixer mixer;
    std::vector<std::uint32_t> players;
    const auto encoded = ReadSound();
    for (std::size_t i = 0; i < EncodedMusicMixer::kMaximumPlayers; ++i) {
        const auto player = mixer.Create();
        players.push_back(player);
        REQUIRE(mixer.SetEncoded(player, encoded));
        REQUIRE(mixer.BeginPrepare(player));
        CHECK_FALSE(mixer.BeginPrepare(player));
    }
    CHECK_THROWS_AS(static_cast<void>(mixer.Create()), std::length_error);
    CHECK(mixer.CachedDecodedBytes() <= players.size() * (encoded.size() + 16384U));
    for (auto player : players) {
        mixer.Reset(player);
        CHECK(mixer.PollPrepare(player) == EncodedMusicMixer::PrepareStatus::failed);
        REQUIRE(mixer.SetEncoded(player, encoded));
        REQUIRE(mixer.BeginPrepare(player));
        mixer.Destroy(player);
        CHECK(mixer.PollPrepare(player) == EncodedMusicMixer::PrepareStatus::failed);
    }
    CHECK(mixer.CachedDecodedBytes() == 0U);
}

TEST_CASE("encoded music rejects aggregate input overflow before preparation") {
    using namespace ogplay::audio;
    EncodedMusicMixer mixer;
    auto a = mixer.Create(), b = mixer.Create(), c = mixer.Create();
    REQUIRE(mixer.SetEncoded(a, std::vector<std::byte>(kMaximumEncodedAudioBytes)));
    REQUIRE(mixer.SetEncoded(b, std::vector<std::byte>(kMaximumEncodedAudioBytes)));
    CHECK_FALSE(mixer.SetEncoded(c, {std::byte{1}}));
    mixer.Destroy(a);
    CHECK(mixer.SetEncoded(c, {std::byte{1}}));
}

TEST_CASE("encoded music stream gains isolate mute without pausing playback") {
    using namespace ogplay::audio;
    EncodedMusicMixer mixer;
    auto a = mixer.Create(), b = mixer.Create();
    REQUIRE(mixer.SetEncoded(a, ReadSound()));
    REQUIRE(mixer.SetEncoded(b, ReadSound()));
    REQUIRE(mixer.Prepare(a)); REQUIRE(mixer.Prepare(b));
    mixer.SetAudioStream(a, 3); mixer.SetAudioStream(b, 4);
    mixer.SetVolume(a, 1, 0); mixer.SetVolume(b, 0, 1);
    mixer.SetStreamGain(3, 0);
    mixer.Start(a); mixer.Start(b);
    std::vector<std::int64_t> pcm(1024 * 2);
    mixer.MixIntoAccumulator(pcm, 48000);
    bool right{};
    for (std::size_t i = 0; i < pcm.size(); i += 2) {
        REQUIRE(pcm[i] == 0); right |= pcm[i+1] != 0;
    }
    CHECK(right);
    CHECK(mixer.PositionMs(a) > 0);
    CHECK(mixer.PositionMs(a) == mixer.PositionMs(b));
}

TEST_CASE("long encoded music prepares without allocating the complete PCM song") {
    using namespace ogplay::audio;
    const auto part = ReadSound("short-mp3.mp3");
    const auto decoded = DecodeEncodedAudio(part);
    constexpr std::size_t repeats = 1500;
    REQUIRE(decoded.interleaved_samples.size() * sizeof(std::int16_t) * repeats > kMaximumDecodedPcmBytes);
    std::vector<std::byte> encoded;
    encoded.reserve(part.size() * repeats);
    for (std::size_t i = 0; i < repeats; ++i) encoded.insert(encoded.end(), part.begin(), part.end());
    const auto bytes = encoded.size();
    REQUIRE(bytes < kMaximumEncodedAudioBytes);
    EncodedMusicMixer mixer;
    const auto player = mixer.Create();
    REQUIRE(mixer.SetEncoded(player, std::move(encoded)));
    REQUIRE(mixer.Prepare(player));
    CHECK(mixer.DurationMs(player) > 600000);
    CHECK(mixer.CachedDecodedBytes() <= bytes + 16384U);
    mixer.Start(player);
    std::vector<std::int64_t> pcm(2048);
    mixer.MixIntoAccumulator(pcm, decoded.sample_rate);
    CHECK(mixer.IsPlaying(player));
    CHECK(mixer.CachedDecodedBytes() <= bytes + 16384U);
}
