#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
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
        [&sound](const std::int32_t resource) {
            return resource == 7 ? sound : std::vector<std::byte>{};
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
        [](const std::int32_t) { return std::vector<std::byte>{}; }};
    CHECK_FALSE(mixer.Load(99));
    REQUIRE(mixer.LoadFailure(99).has_value());
    CHECK_FALSE(mixer.Play(
        ogplay::audio::JavaSoundPoolKind::big, 99, 0, 1.0F));
    CHECK(mixer.LoadedResourceCount() == 0U);
    CHECK(mixer.ActiveVoiceCount() == 0U);
}

TEST_CASE("SoundPool mixer serializes guest controls with host rendering") {
    const auto sound = ReadSound();
    ogplay::audio::JavaSoundPoolMixer mixer{
        [&sound](const std::int32_t) { return sound; }};
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
