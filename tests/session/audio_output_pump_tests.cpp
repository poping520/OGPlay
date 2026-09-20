#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

#include "ogplay/hal/audio.h"
#include "ogplay/session/audio_output_pump.h"

namespace {

class FakeAudioOutput final : public ogplay::hal::AudioOutput {
public:
    explicit FakeAudioOutput(const bool fail_submit = false)
        : fail_submit_(fail_submit) {}

    [[nodiscard]] ogplay::hal::AudioStreamConfig Config() const override {
        return {48000U, 2U, ogplay::hal::AudioSampleFormat::signed_16_le};
    }
    [[nodiscard]] bool IsStarted() const override { return started_; }
    [[nodiscard]] std::uint64_t QueuedFrames() const override {
        return queued_frames_.load(std::memory_order_relaxed);
    }
    void Start() override { started_ = true; }
    void Stop() override { started_ = false; }
    void Submit(const std::span<const std::byte> interleaved_samples) override {
        if (fail_submit_) throw std::runtime_error("audio device failed");
        submits_.fetch_add(1U, std::memory_order_relaxed);
        queued_frames_.fetch_add(interleaved_samples.size() / 4U,
                                 std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t Submits() const {
        return submits_.load(std::memory_order_relaxed);
    }

private:
    bool fail_submit_{};
    bool started_{};
    std::atomic<std::uint32_t> submits_{};
    std::atomic<std::uint64_t> queued_frames_{};
};

[[nodiscard]] std::uint64_t Fnv1a(const std::span<const std::int16_t> samples) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto sample : samples) {
        const auto bits = static_cast<std::uint16_t>(sample);
        hash ^= static_cast<std::uint8_t>(bits);
        hash *= 1099511628211ULL;
        hash ^= static_cast<std::uint8_t>(bits >> 8U);
        hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace

TEST_CASE("session audio pump advances offline frames from Clock ticks") {
    std::uint32_t mixes{};
    ogplay::session::AudioOutputPump pump(
        [&](const std::span<std::int16_t> output, const std::uint32_t rate) {
            ++mixes;
            CHECK(rate == 48000U);
            CHECK(output.size() == 8U);
            output[0] = 12;
            return 4U;
        },
        nullptr, 48000U, 2U);
    std::array<std::int16_t, 8> buffer{};
    CHECK(pump.MixOffline(buffer) == 4U);
    CHECK(buffer[0] == 12);
    CHECK(mixes == 1U);
    CHECK(pump.FramesForTicks(1'000'000'000U, 1'000'000'000U) == 48000U);
    CHECK(pump.FramesForTicks(500'000'000U, 1'000'000'000U) == 24000U);
}

TEST_CASE("session audio pump offline hash is independent of wall time") {
    ogplay::session::AudioOutputPump pump(
        [](const std::span<std::int16_t> output, const std::uint32_t) {
            for (std::size_t index = 0; index < output.size(); ++index) {
                output[index] = static_cast<std::int16_t>(index * 17U);
            }
            return output.size() / 2U;
        },
        nullptr, 48000U, 2U);
    std::array<std::int16_t, 16> first{};
    std::array<std::int16_t, 16> second{};
    CHECK(pump.MixOffline(first) == 8U);
    CHECK(pump.MixOffline(second) == 8U);
    CHECK(first == second);
    CHECK(Fnv1a(first) == Fnv1a(second));
    CHECK(Fnv1a(first) != 0U);
    CHECK(pump.FramesForTicks(1U, 96000U) == 0U);
    CHECK(pump.FramesForTicks(1U, 96000U) == 1U);
}

TEST_CASE("session audio pump records device failure without terminating") {
    FakeAudioOutput output(true);
    output.Start();
    std::atomic mixes{0U};
    ogplay::session::AudioOutputPump pump(
        [&](const std::span<std::int16_t> buffer, const std::uint32_t) {
            mixes.fetch_add(1U, std::memory_order_relaxed);
            buffer[0] = 1;
            buffer[1] = 1;
            return 1U;
        },
        &output, 48000U, 2U);
    pump.StartRealtime();
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!pump.DeviceFailed() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(pump.DeviceFailed());
    pump.Stop();
}

TEST_CASE("session audio pump preserves mixer failures") {
    FakeAudioOutput output;
    output.Start();
    ogplay::session::AudioOutputPump pump(
        [](const std::span<std::int16_t>, const std::uint32_t) -> std::size_t {
            throw std::runtime_error("mixer callback failed");
        },
        &output, 48000U, 2U);
    pump.StartRealtime();
    bool observed{};
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!observed && std::chrono::steady_clock::now() < deadline) {
        try {
            pump.RethrowWorkerFailure();
        } catch (const std::runtime_error& error) {
            CHECK(std::string_view(error.what()) == "mixer callback failed");
            observed = true;
        }
        std::this_thread::yield();
    }
    CHECK(observed);
    CHECK_FALSE(pump.DeviceFailed());
    pump.Stop();
}

TEST_CASE("session audio pump suspends realtime submit") {
    FakeAudioOutput output;
    output.Start();
    ogplay::session::AudioOutputPump pump(
        [](const std::span<std::int16_t> buffer, const std::uint32_t) {
            buffer[0] = 2;
            buffer[1] = 3;
            return 1U;
        },
        &output, 48000U, 2U);
    pump.StartRealtime();
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (output.Submits() == 0U &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(output.Submits() > 0U);
    pump.SetSuspended(true);
    const auto frozen = output.Submits();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(output.Submits() == frozen);
    pump.Stop();
}
