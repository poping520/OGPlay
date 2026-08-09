#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <stdexcept>

#include "ogplay/hal/audio.h"

#if OGPLAY_TEST_HAS_SDL3

TEST_CASE("SDL dummy audio output queues complete PCM frames") {
    auto output = ogplay::hal::CreateSdlAudioOutput(
        {48000U, 2U, ogplay::hal::AudioSampleFormat::signed_16_le},
        ogplay::hal::AudioBackend::dummy);
    CHECK(output->Config() == ogplay::hal::AudioStreamConfig{
                                  48000U, 2U,
                                  ogplay::hal::AudioSampleFormat::signed_16_le});
    CHECK_FALSE(output->IsStarted());
    const std::array<std::byte, 16> samples{};
    output->Submit(samples);
    CHECK(output->QueuedFrames() == 4U);
    CHECK_THROWS_AS(output->Submit(std::span{samples}.first(15U)),
                    std::invalid_argument);
    output->Start();
    CHECK(output->IsStarted());
    output->Start();
    output->Stop();
    CHECK_FALSE(output->IsStarted());
    output->Stop();
}

TEST_CASE("SDL audio output rejects invalid stream configs") {
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::hal::CreateSdlAudioOutput(
            {0U, 2U, ogplay::hal::AudioSampleFormat::signed_16_le},
            ogplay::hal::AudioBackend::dummy)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::hal::CreateSdlAudioOutput(
            {48000U, 0U, ogplay::hal::AudioSampleFormat::signed_16_le},
            ogplay::hal::AudioBackend::dummy)),
        std::invalid_argument);
}

#else

TEST_CASE("SDL-disabled builds fail the audio factory explicitly") {
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::hal::CreateSdlAudioOutput(
            {48000U, 2U, ogplay::hal::AudioSampleFormat::signed_16_le})),
        "SDL3 support is disabled in this build", std::runtime_error);
}

#endif
