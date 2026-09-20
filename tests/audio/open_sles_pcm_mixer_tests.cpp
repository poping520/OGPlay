#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
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

TEST_CASE("AudioTrack byte budget blocks until playback frees space") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto player = mixer.CreatePlayer({48000U, 1U, 16U}, 255U);
    const auto pcm = Pcm16({1000, 2000, 3000, 4000});
    REQUIRE(mixer.EnqueueBlocking(player, pcm, pcm.size()) ==
            ogplay::audio::OpenSlesEnqueueResult::enqueued);
    CHECK(mixer.QueuedBytes(player) == pcm.size());

    std::atomic result{ogplay::audio::OpenSlesEnqueueResult::interrupted};
    std::jthread writer([&] {
        result = mixer.EnqueueBlocking(player, pcm, pcm.size());
    });
    bool parked{};
    for (std::size_t attempt = 0; attempt < 100000U; ++attempt) {
        if (mixer.BlockingWriterCount() == 1U) {
            parked = true;
            break;
        }
        std::this_thread::yield();
    }
    if (!parked) static_cast<void>(mixer.InterruptBlockingWaits());
    REQUIRE(parked);
    CHECK(mixer.QueuedBytes(player) == pcm.size());

    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);
    std::array<std::int16_t, 8> output{};
    static_cast<void>(mixer.MixAdditiveStereoPcm16(output, 48000U));
    writer.join();
    CHECK(result.load() == ogplay::audio::OpenSlesEnqueueResult::enqueued);
    CHECK(mixer.QueuedBytes(player) == pcm.size());
}

TEST_CASE("AudioTrack blocking enqueue is interrupted by teardown") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto player = mixer.CreatePlayer({48000U, 1U, 16U}, 255U);
    const auto pcm = Pcm16({1000, 2000, 3000, 4000});
    REQUIRE(mixer.EnqueueBlocking(player, pcm, pcm.size()) ==
            ogplay::audio::OpenSlesEnqueueResult::enqueued);

    std::atomic result{ogplay::audio::OpenSlesEnqueueResult::enqueued};
    std::jthread writer([&] {
        result = mixer.EnqueueBlocking(player, pcm, pcm.size());
    });
    bool parked{};
    for (std::size_t attempt = 0; attempt < 100000U; ++attempt) {
        if (mixer.BlockingWriterCount() == 1U) {
            parked = true;
            break;
        }
        std::this_thread::yield();
    }
    const auto interrupted = mixer.InterruptBlockingWaits();
    writer.join();
    REQUIRE(parked);
    CHECK(interrupted == 1U);
    CHECK(result.load() == ogplay::audio::OpenSlesEnqueueResult::interrupted);
    CHECK(mixer.EnqueueBlocking(player, pcm, pcm.size()) ==
          ogplay::audio::OpenSlesEnqueueResult::interrupted);
}

TEST_CASE("AudioTrack blocking enqueue wakes when its player is destroyed") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto player = mixer.CreatePlayer({48000U, 1U, 16U}, 255U);
    const auto pcm = Pcm16({1000, 2000, 3000, 4000});
    REQUIRE(mixer.EnqueueBlocking(player, pcm, pcm.size()) ==
            ogplay::audio::OpenSlesEnqueueResult::enqueued);

    std::atomic result{ogplay::audio::OpenSlesEnqueueResult::enqueued};
    std::jthread writer([&] {
        result = mixer.EnqueueBlocking(player, pcm, pcm.size());
    });
    while (mixer.BlockingWriterCount() == 0U) std::this_thread::yield();
    mixer.DestroyPlayer(player);
    writer.join();
    CHECK(result.load() ==
          ogplay::audio::OpenSlesEnqueueResult::player_destroyed);
}

TEST_CASE("AudioTrack stop interrupts a blocked enqueue") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto player = mixer.CreatePlayer({48000U, 1U, 16U}, 255U);
    mixer.SetPlayerKind(
        player, ogplay::audio::OpenSlesPlayerKind::audio_track_stream);
    const auto pcm = Pcm16({1000, 2000, 3000, 4000});
    REQUIRE(mixer.EnqueueBlocking(player, pcm, pcm.size()) ==
            ogplay::audio::OpenSlesEnqueueResult::enqueued);
    std::atomic result{ogplay::audio::OpenSlesEnqueueResult::enqueued};
    std::jthread writer([&] {
        result = mixer.EnqueueBlocking(player, pcm, pcm.size());
    });
    while (mixer.BlockingWriterCount() == 0U) std::this_thread::yield();
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::stopped);
    writer.join();
    CHECK(result.load() == ogplay::audio::OpenSlesEnqueueResult::interrupted);
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

TEST_CASE("OpenSL mixer keeps resample phase across buffer boundaries") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto player = mixer.CreatePlayer({24000U, 1U, 16U}, 4U);
    const auto first = Pcm16({0, 20000});
    const auto second = Pcm16({20000, 0});
    REQUIRE(mixer.Enqueue(player, first));
    REQUIRE(mixer.Enqueue(player, second));
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);
    std::array<std::int16_t, 16> split{};
    static_cast<void>(mixer.MixAdditiveStereoPcm16(
        std::span{split}.first(2U), 48000U));
    static_cast<void>(mixer.MixAdditiveStereoPcm16(
        std::span{split}.subspan(2U, 2U), 48000U));
    static_cast<void>(mixer.MixAdditiveStereoPcm16(
        std::span{split}.subspan(4U, 12U), 48000U));

    ogplay::audio::OpenSlesPcmMixer whole;
    const auto other = whole.CreatePlayer({24000U, 1U, 16U}, 4U);
    REQUIRE(whole.Enqueue(other, Pcm16({0, 20000, 20000, 0})));
    whole.SetPlayState(other, ogplay::audio::OpenSlesPlayState::playing);
    std::array<std::int16_t, 16> combined{};
    static_cast<void>(whole.MixAdditiveStereoPcm16(combined, 48000U));
    CHECK(split == combined);
}

TEST_CASE("OpenSL mixer cancels opposite signals without intermediate clip") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto left = mixer.CreatePlayer({48000U, 1U, 16U}, 1U);
    const auto right = mixer.CreatePlayer({48000U, 1U, 16U}, 1U);
    REQUIRE(mixer.Enqueue(left, Pcm16({30000})));
    REQUIRE(mixer.Enqueue(right, Pcm16({-30000})));
    mixer.SetPlayState(left, ogplay::audio::OpenSlesPlayState::playing);
    mixer.SetPlayState(right, ogplay::audio::OpenSlesPlayState::playing);
    std::array<std::int16_t, 2> output{};
    static_cast<void>(mixer.MixAdditiveStereoPcm16(output, 48000U));
    CHECK(output == std::array<std::int16_t, 2>{0, 0});
}

TEST_CASE("AudioTrack static buffers replay after stop without being consumed") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto player = mixer.CreatePlayer({48000U, 1U, 16U}, 1U);
    mixer.SetPlayerKind(player, ogplay::audio::OpenSlesPlayerKind::audio_track_static);
    REQUIRE(mixer.Enqueue(player, Pcm16({1000, 2000})));
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);
    std::array<std::int16_t, 4> first{};
    static_cast<void>(mixer.MixAdditiveStereoPcm16(first, 48000U));
    CHECK(first == std::array<std::int16_t, 4>{1000, 1000, 2000, 2000});
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::stopped);
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);
    std::array<std::int16_t, 4> second{};
    static_cast<void>(mixer.MixAdditiveStereoPcm16(second, 48000U));
    CHECK(second == first);
}

TEST_CASE("OpenSL mixer 44.1 kHz resample matches across 1 and 17 frame pumps") {
    std::vector<std::byte> pcm;
    for (std::int16_t value = 0; value < 64; ++value) {
        const auto frame = Pcm16({static_cast<std::int16_t>(value * 400)});
        pcm.insert(pcm.end(), frame.begin(), frame.end());
    }
    const auto mix = [&](const std::size_t frames_per_call) {
        ogplay::audio::OpenSlesPcmMixer mixer;
        const auto player = mixer.CreatePlayer({44100U, 1U, 16U}, 1U);
        REQUIRE(mixer.Enqueue(player, pcm));
        mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);
        std::array<std::int16_t, 140> output{};
        std::size_t filled{};
        while (filled + frames_per_call <= 70U) {
            static_cast<void>(mixer.MixAdditiveStereoPcm16(
                std::span{output}.subspan(filled * 2U, frames_per_call * 2U),
                48000U));
            filled += frames_per_call;
        }
        if (filled < 70U) {
            static_cast<void>(mixer.MixAdditiveStereoPcm16(
                std::span{output}.subspan(filled * 2U, (70U - filled) * 2U),
                48000U));
        }
        return output;
    };
    CHECK(mix(1U) == mix(17U));
    CHECK(mix(1U) == mix(70U));
}

TEST_CASE("OpenSL mixer output is independent of mix block size") {
    const auto pcm = Pcm16({1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000});
    const auto mix = [&](const std::size_t frames_per_call) {
        ogplay::audio::OpenSlesPcmMixer mixer;
        const auto player = mixer.CreatePlayer({48000U, 1U, 16U}, 1U);
        REQUIRE(mixer.Enqueue(player, pcm));
        mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);
        std::array<std::int16_t, 16> output{};
        for (std::size_t frame = 0; frame < 8U; frame += frames_per_call) {
            static_cast<void>(mixer.MixAdditiveStereoPcm16(
                std::span{output}.subspan(frame * 2U, frames_per_call * 2U),
                48000U));
        }
        return output;
    };
    CHECK(mix(1U) == mix(8U));
    CHECK(mix(1U) == mix(2U));
}

TEST_CASE("OpenSL mixer Clear resets play_index") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto player = mixer.CreatePlayer({48000U, 1U, 16U}, 2U);
    REQUIRE(mixer.Enqueue(player, Pcm16({1})));
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);
    std::array<std::int16_t, 2> output{};
    static_cast<void>(mixer.MixAdditiveStereoPcm16(output, 48000U));
    CHECK(mixer.QueueState(player).play_index == 1U);
    mixer.Clear(player);
    CHECK(mixer.QueueState(player).play_index == 0U);
    CHECK(mixer.QueueState(player).count == 0U);
}

TEST_CASE("OpenSL mixer 8 kHz resample matches 1 17 and 1024 frame pumps") {
    std::vector<std::byte> pcm;
    for (std::int16_t value = 0; value < 200; ++value) {
        const auto frame = Pcm16({static_cast<std::int16_t>(value * 50)});
        pcm.insert(pcm.end(), frame.begin(), frame.end());
    }
    const auto mix = [&](const std::size_t frames_per_call) {
        ogplay::audio::OpenSlesPcmMixer mixer;
        const auto player = mixer.CreatePlayer({8000U, 1U, 16U}, 1U);
        REQUIRE(mixer.Enqueue(player, pcm));
        mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);
        std::array<std::int16_t, 2400> output{};
        std::size_t filled{};
        const std::size_t total = 1200U;
        while (filled < total) {
            const auto chunk = std::min(frames_per_call, total - filled);
            static_cast<void>(mixer.MixAdditiveStereoPcm16(
                std::span{output}.subspan(filled * 2U, chunk * 2U), 48000U));
            filled += chunk;
        }
        return output;
    };
    CHECK(mix(1U) == mix(17U));
    CHECK(mix(1U) == mix(1024U));
}

TEST_CASE("AudioTrack static loop and rate replay the same buffer") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto player = mixer.CreatePlayer({48000U, 1U, 16U}, 1U);
    mixer.SetPlayerKind(player, ogplay::audio::OpenSlesPlayerKind::audio_track_static);
    mixer.SetLoop(player, 0U, 2U, 1);
    REQUIRE(mixer.Enqueue(player, Pcm16({1000, 2000})));
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);
    std::array<std::int16_t, 8> output{};
    static_cast<void>(mixer.MixAdditiveStereoPcm16(output, 48000U));
    CHECK(output == std::array<std::int16_t, 8>{
                        1000, 1000, 2000, 2000, 1000, 1000, 2000, 2000});
    mixer.SetPlaybackRate(player, 2.0F);
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::stopped);
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);
    std::array<std::int16_t, 4> doubled{};
    static_cast<void>(mixer.MixAdditiveStereoPcm16(doubled, 48000U));
    CHECK(doubled[0] == 1000);
    CHECK(mixer.PositionFrames(player) >= 1U);
}

TEST_CASE("AudioTrack blocking enqueue wakes a second writer") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto player = mixer.CreatePlayer({48000U, 1U, 16U}, 8U);
    const auto pcm = Pcm16({1000, 2000});
    REQUIRE(mixer.EnqueueBlocking(player, pcm, pcm.size()) ==
            ogplay::audio::OpenSlesEnqueueResult::enqueued);
    std::atomic<int> first{-1};
    std::atomic<int> second{-1};
    std::atomic started{0U};
    std::jthread writer_a([&] {
        started.fetch_add(1U);
        first = static_cast<int>(mixer.EnqueueBlocking(player, pcm, pcm.size()));
    });
    std::jthread writer_b([&] {
        started.fetch_add(1U);
        second = static_cast<int>(mixer.EnqueueBlocking(player, pcm, pcm.size()));
    });
    while (started.load() < 2U) std::this_thread::yield();
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    std::array<std::int16_t, 8> output{};
    while ((first.load() < 0 || second.load() < 0) &&
           std::chrono::steady_clock::now() < deadline) {
        static_cast<void>(mixer.MixAdditiveStereoPcm16(output, 48000U));
        std::this_thread::yield();
    }
    static_cast<void>(mixer.InterruptBlockingWaits());
    writer_a.join();
    writer_b.join();
    CHECK(first.load() ==
          static_cast<int>(ogplay::audio::OpenSlesEnqueueResult::enqueued));
    CHECK(second.load() ==
          static_cast<int>(ogplay::audio::OpenSlesEnqueueResult::enqueued));
}

TEST_CASE("AudioTrack stream snapshot counts underrun episodes and frames") {
    ogplay::audio::OpenSlesPcmMixer mixer;
    const auto player = mixer.CreatePlayer({48000U, 1U, 16U}, 4U);
    mixer.SetPlayerKind(
        player, ogplay::audio::OpenSlesPlayerKind::audio_track_stream);
    mixer.SetPlayState(player, ogplay::audio::OpenSlesPlayState::playing);

    std::array<std::int16_t, 20> first_gap{};
    static_cast<void>(mixer.MixAdditiveStereoPcm16(first_gap, 48000U));
    std::array<std::int16_t, 10> same_gap{};
    static_cast<void>(mixer.MixAdditiveStereoPcm16(same_gap, 48000U));
    auto snapshot = mixer.Snapshot(player);
    CHECK(snapshot.consumed_source_frames == 0U);
    CHECK(snapshot.underrun_output_frames == 15U);
    CHECK(snapshot.underrun_count == 1U);

    REQUIRE(mixer.Enqueue(player, Pcm16({1000, 2000})));
    std::array<std::int16_t, 4> recovered{};
    static_cast<void>(mixer.MixAdditiveStereoPcm16(recovered, 48000U));
    std::array<std::int16_t, 6> second_gap{};
    static_cast<void>(mixer.MixAdditiveStereoPcm16(second_gap, 48000U));
    snapshot = mixer.Snapshot(player);
    CHECK(snapshot.queued_bytes == 0U);
    CHECK(snapshot.consumed_source_frames == 2U);
    CHECK(snapshot.underrun_output_frames == 18U);
    CHECK(snapshot.underrun_count == 2U);
}
