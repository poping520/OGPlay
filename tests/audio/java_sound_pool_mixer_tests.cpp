#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

#include "ogplay/audio/java_sound_pool_mixer.h"

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

TEST_CASE("SoundPool mixer loads decodes controls and renders a real voice") {
    const auto sound = ReadSound();
    ogplay::audio::JavaSoundPoolMixer mixer{
        [&sound](const ogplay::audio::EncodedAudioSource& source) {
            return source.resource == 7 ? sound : std::vector<std::byte>{};
        }};
    CHECK(mixer.Enabled());
    CHECK(mixer.Load(7));
    CHECK(mixer.LoadedResourceCount() == 1U);
    CHECK(mixer.Play(ogplay::audio::JavaSoundPoolKind::pool, 7, 2, 0.5F));
    std::vector<std::int16_t> output(2048U * 2U);
    CHECK(mixer.RenderStereoPcm16(output, 48000U) == 2048U);
    CHECK(std::ranges::any_of(output, [](const auto sample) {
        return sample != 0;
    }));
    mixer.Pause(ogplay::audio::JavaSoundPoolKind::pool, 7, 2);
    CHECK(mixer.RenderStereoPcm16(output, 48000U) == 2048U);
    CHECK(std::ranges::all_of(output, [](const auto sample) {
        return sample == 0;
    }));
    mixer.Resume(ogplay::audio::JavaSoundPoolKind::pool, 7, 2);
    mixer.SetPitch(ogplay::audio::JavaSoundPoolKind::pool, 7, 2, 1.5F);
    mixer.Reset(ogplay::audio::JavaSoundPoolKind::pool, 7, 2);
    CHECK(mixer.RenderStereoPcm16(output, 48000U) == 2048U);
    CHECK(std::ranges::any_of(output, [](const auto sample) {
        return sample != 0;
    }));
    mixer.Stop(ogplay::audio::JavaSoundPoolKind::pool, 7, 2);
    CHECK(mixer.ActiveVoiceCount() == 0U);
}

TEST_CASE("SoundPool mixer keeps unavailable resources explicit") {
    ogplay::audio::JavaSoundPoolMixer mixer{
        [](const ogplay::audio::EncodedAudioSource&) {
            return std::vector<std::byte>{};
        }};
    CHECK_FALSE(mixer.Load(99));
    REQUIRE(mixer.LoadFailure(99).has_value());
    CHECK_FALSE(mixer.Play(
        ogplay::audio::JavaSoundPoolKind::big, 99, 0, 1.0F));
    CHECK(mixer.LoadedResourceCount() == 0U);
    CHECK(mixer.ActiveVoiceCount() == 0U);
}

TEST_CASE("SoundPool mixer decodes WAV windows") {
    std::vector<std::byte> wav{
        std::byte{'R'}, std::byte{'I'}, std::byte{'F'}, std::byte{'F'},
        std::byte{36}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{'W'}, std::byte{'A'}, std::byte{'V'}, std::byte{'E'},
        std::byte{'f'}, std::byte{'m'}, std::byte{'t'}, std::byte{' '},
        std::byte{16}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{1}, std::byte{0}, std::byte{1}, std::byte{0},
        std::byte{0x40}, std::byte{0x1f}, std::byte{0}, std::byte{0},
        std::byte{0x80}, std::byte{0x3e}, std::byte{0}, std::byte{0},
        std::byte{2}, std::byte{0}, std::byte{16}, std::byte{0},
        std::byte{'d'}, std::byte{'a'}, std::byte{'t'}, std::byte{'a'},
        std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{0xe8}, std::byte{0x03}};
    ogplay::audio::JavaSoundPoolMixer mixer{
        [&wav](const ogplay::audio::EncodedAudioSource&) { return wav; }};
    REQUIRE(mixer.Load(3));
    REQUIRE(mixer.Play(ogplay::audio::JavaSoundPoolKind::pool, 3, 1, 1.0F));
    std::vector<std::int16_t> output(16U);
    CHECK(mixer.RenderStereoPcm16(output, 8000U) == 8U);
    CHECK(std::ranges::any_of(output, [](const auto sample) {
        return sample != 0;
    }));
}

TEST_CASE("SoundPool mixer serializes guest controls with host rendering") {
    const auto sound = ReadSound();
    ogplay::audio::JavaSoundPoolMixer mixer{
        [&sound](const ogplay::audio::EncodedAudioSource&) { return sound; }};
    REQUIRE(mixer.Load(1));
    REQUIRE(mixer.Play(
        ogplay::audio::JavaSoundPoolKind::pool, 1, 1, 1.0F));
    std::jthread renderer{[&mixer] {
        std::vector<std::int16_t> output(64U * 2U);
        for (std::size_t iteration = 0; iteration < 64U; ++iteration) {
            static_cast<void>(mixer.RenderStereoPcm16(output, 48000U));
        }
    }};
    for (std::size_t iteration = 0; iteration < 64U; ++iteration) {
        mixer.SetVolume(ogplay::audio::JavaSoundPoolKind::pool, 1, 1,
                        iteration % 2U == 0U ? 0.25F : 0.75F);
        mixer.Pause(ogplay::audio::JavaSoundPoolKind::pool, 1, 1);
        mixer.Resume(ogplay::audio::JavaSoundPoolKind::pool, 1, 1);
    }
    renderer.join();
    CHECK(mixer.LoadedResourceCount() == 1U);
}

TEST_CASE("SoundPool mixer treats path revision as a distinct cache key") {
    const auto sound = ReadSound();
    std::uint32_t loads{};
    ogplay::audio::JavaSoundPoolMixer mixer{
        [&](const ogplay::audio::EncodedAudioSource&) {
            ++loads;
            return sound;
        }};
    ogplay::audio::EncodedAudioSource path;
    path.kind = ogplay::audio::EncodedAudioSource::Kind::vfs_path;
    path.name = "/sdcard/music.ogg";
    path.revision = 1U;
    REQUIRE(mixer.Load(path));
    path.revision = 2U;
    REQUIRE(mixer.Load(path));
    CHECK(loads == 2U);
    CHECK(mixer.LoadedResourceCount() == 2U);
}

TEST_CASE("SoundPool mixer isolates pools and honors loop priority and rate") {
    const auto sound = ReadSound();
    ogplay::audio::JavaSoundPoolMixer mixer{
        [&sound](const ogplay::audio::EncodedAudioSource&) { return sound; }};
    const auto first = mixer.CreatePool(1);
    const auto second = mixer.CreatePool(1);
    const auto a = mixer.LoadSample(first, 1);
    const auto b = mixer.LoadSample(second, 1);
    REQUIRE(a != 0);
    REQUIRE(b != 0);
    const auto stream_a =
        mixer.PlaySample(first, a, 1.0F, 0.0F, 1, 0, 1.0F);
    const auto stream_b =
        mixer.PlaySample(second, b, 0.0F, 1.0F, 1, 0, 1.0F);
    REQUIRE(stream_a != 0);
    REQUIRE(stream_b != 0);
    CHECK(stream_a != stream_b);
    const auto preempted =
        mixer.PlaySample(first, a, 1.0F, 1.0F, 2, 0, 1.0F);
    REQUIRE(preempted != 0);
    mixer.SetStreamLoop(stream_b, -1);
    mixer.SetStreamRate(stream_b, 0.5F);
    std::vector<std::int16_t> pcm(256U * 2U);
    CHECK(mixer.RenderStereoPcm16(pcm, 48000U) == 256U);
    CHECK(std::ranges::any_of(pcm, [](const std::int16_t sample) {
        return sample != 0;
    }));
    CHECK(mixer.UnloadSample(first, a));
    CHECK(mixer.LoadedResourceCount() == 1U);
    mixer.DestroyPool(first);
    mixer.DestroyPool(second);
    CHECK(mixer.LoadedResourceCount() == 0U);
}

TEST_CASE("SoundPool auto pause is pool-local and preserves manual pause") {
    const auto sound = ReadSound();
    ogplay::audio::JavaSoundPoolMixer mixer{
        [&sound](const ogplay::audio::EncodedAudioSource&) { return sound; }};
    const auto first = mixer.CreatePool(2);
    const auto second = mixer.CreatePool(1);
    const auto a = mixer.LoadSample(first, 1);
    const auto b = mixer.LoadSample(first, 2);
    const auto c = mixer.LoadSample(second, 3);
    const auto stream_a = mixer.PlaySample(first, a, 1, 1, 1, -1, 1);
    const auto stream_b = mixer.PlaySample(first, b, 1, 1, 1, -1, 1);
    const auto stream_c = mixer.PlaySample(second, c, 1, 1, 1, -1, 1);
    mixer.PauseStream(stream_b);
    mixer.AutoPausePool(first);
    mixer.AutoResumePool(first);
    std::vector<std::int16_t> pcm(32U * 2U);
    static_cast<void>(mixer.RenderStereoPcm16(pcm, 48000U));
    CHECK(mixer.ActiveVoiceCount() == 3U);
    mixer.StopStream(stream_a);
    mixer.StopStream(stream_b);
    mixer.StopStream(stream_c);
}

TEST_CASE("SoundPool stream gains affect only the selected pool audio stream") {
    const auto encoded = ReadSound();
    ogplay::audio::JavaSoundPoolMixer mixer([&](const auto&) { return encoded; });
    const auto a = mixer.CreatePool(1, 3), b = mixer.CreatePool(1, 4);
    const auto source = ogplay::audio::EncodedAudioSource::Resource(1);
    const auto sa = mixer.LoadSample(a, source), sb = mixer.LoadSample(b, source);
    REQUIRE(mixer.PlaySample(a, sa, 1, 0, 1, -1, 1) != 0);
    REQUIRE(mixer.PlaySample(b, sb, 0, 1, 1, -1, 1) != 0);
    mixer.SetStreamGain(4, 0);
    std::vector<std::int64_t> pcm(2048);
    mixer.MixIntoAccumulator(pcm, 48000);
    bool left{};
    for (std::size_t i = 0; i < pcm.size(); i += 2) {
        left |= pcm[i] != 0; REQUIRE(pcm[i+1] == 0);
    }
    CHECK(left);
    mixer.SetStreamGain(4, 1); mixer.SetStreamGain(3, 0);
    std::fill(pcm.begin(), pcm.end(), 0);
    mixer.MixIntoAccumulator(pcm, 48000);
    bool right{};
    for (std::size_t i = 0; i < pcm.size(); i += 2) {
        REQUIRE(pcm[i] == 0); right |= pcm[i+1] != 0;
    }
    CHECK(right);
}
