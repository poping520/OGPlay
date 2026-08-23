#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "ogplay/audio/open_sles_pcm_mixer.h"

namespace {

[[nodiscard]] std::vector<std::byte> Pcm16(
    const std::initializer_list<std::int16_t> samples) {
    std::vector<std::byte> bytes;
    bytes.reserve(samples.size() * 2U);
    for (const auto sample : samples) {
        const auto value = static_cast<std::uint16_t>(sample);
        bytes.push_back(static_cast<std::byte>(value & 0xffU));
        bytes.push_back(static_cast<std::byte>(value >> 8U));
    }
    return bytes;
}

}  // namespace

TEST_CASE("OpenSL mixer queues plays pauses clears and reports consumption") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto player = mixer.CreatePlayer({48000U, 1U, 16U}, 1U);
    const auto pcm = Pcm16({1000, 2000, 3000, 4000});
    CHECK(mixer.Enqueue(player, pcm));
    CHECK_FALSE(mixer.Enqueue(player, pcm));
    CHECK(mixer.QueueState(player).count == 1U);
    std::array<std::int16_t, 8> output{};
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::paused);
    CHECK(mixer.MixAdditiveStereoPcm16(output, 48000U).empty());
    CHECK(output == std::array<std::int16_t, 8>{});
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);
    const auto consumed = mixer.MixAdditiveStereoPcm16(output, 48000U);
    REQUIRE(consumed.size() == 1U);
    CHECK(consumed[0].player == player);
    CHECK(output == std::array<std::int16_t, 8>{
                        1000, 1000, 2000, 2000, 3000, 3000, 4000, 4000});
    CHECK(mixer.QueueState(player).play_index == 1U);
    mixer.Clear(player);
    mixer.DestroyPlayer(player);
    CHECK_FALSE(mixer.HasPlayer(player));
    CHECK_THROWS(static_cast<void>(mixer.QueueState(player)));
}

TEST_CASE("OpenSL mixer decodes PCM8 resamples and applies volume mute pan") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto player = mixer.CreatePlayer({24000U, 1U, 8U}, 2U);
    const std::array pcm{std::byte{128}, std::byte{192}, std::byte{255}};
    REQUIRE(mixer.Enqueue(player, pcm));
    mixer.SetVolume(player, -602);
    mixer.SetStereoPosition(player, 1000);
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);
    std::array<std::int16_t, 8> output{};
    static_cast<void>(mixer.MixAdditiveStereoPcm16(output, 48000U));
    CHECK(output[0] == 0);
    CHECK(output[1] == 0);
    CHECK(output[2] == 0);
    CHECK(output[3] > 3000);
    CHECK(output[4] == 0);
    CHECK(output[5] > output[3]);
    mixer.SetMute(player, true);
    output.fill(7);
    const auto muted_consumed =
        mixer.MixAdditiveStereoPcm16(output, 48000U);
    CHECK(output == std::array<std::int16_t, 8>{7, 7, 7, 7, 7, 7, 7, 7});
    REQUIRE(muted_consumed.size() == 1U);
    CHECK(muted_consumed[0].player == player);
    CHECK(mixer.QueueState(player).count == 0U);
    CHECK(mixer.QueueState(player).play_index == 1U);
}

TEST_CASE("OpenSL mixer additively saturates multiple players and serializes controls") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto left = mixer.CreatePlayer({48000U, 2U, 16U}, 2U);
    const auto right = mixer.CreatePlayer({48000U, 2U, 16U}, 2U);
    const auto pcm = Pcm16({30000, 30000, 30000, 30000});
    REQUIRE(mixer.Enqueue(left, pcm));
    REQUIRE(mixer.Enqueue(right, pcm));
    mixer.SetPlayState(left, ogplay::audio::OpenSlesPlayState::playing);
    mixer.SetPlayState(right, ogplay::audio::OpenSlesPlayState::playing);
    std::array<std::int16_t, 4> output{1000, 1000, 1000, 1000};
    const auto consumed = mixer.MixAdditiveStereoPcm16(output, 48000U);
    CHECK(consumed.size() == 2U);
    CHECK(output == std::array<std::int16_t, 4>{32767, 32767, 32767, 32767});

    const auto concurrent = mixer.CreatePlayer({48000U, 1U, 16U}, 8U);
    std::jthread renderer{[&] {
        std::array<std::int16_t, 16> block{};
        for (std::size_t iteration = 0; iteration < 64U; ++iteration) {
            static_cast<void>(mixer.MixAdditiveStereoPcm16(block, 48000U));
        }
    }};
    for (std::size_t iteration = 0; iteration < 64U; ++iteration) {
        mixer.SetMute(concurrent, iteration % 2U == 0U);
        mixer.SetPlayState(concurrent, ogplay::audio::OpenSlesPlayState::paused);
    }
    renderer.join();
    CHECK(mixer.HasPlayer(concurrent));
}
