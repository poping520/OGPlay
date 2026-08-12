#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <vector>

#include "ogplay/video/ffmpeg_video_player.h"
#include "ogplay/video/video_player.h"

namespace {

[[nodiscard]] std::filesystem::path FixturePath() {
    return std::filesystem::path{OGPLAY_SOURCE_DIR} /
           "tests/fixtures/video/short-mp4v-aac.mp4";
}

}  // namespace

TEST_CASE("ffmpeg availability probe is stable and diagnosable") {
    const bool available = ogplay::video::FfmpegAvailable();
    const std::string reason = ogplay::video::FfmpegUnavailableReason();
    if (available) {
        CHECK(reason.empty());
    } else {
        CHECK_FALSE(reason.empty());
        CHECK_THROWS_AS((void)ogplay::video::OpenFfmpegVideo(FixturePath()),
                        ogplay::video::VideoPlayerError);
    }
    CHECK(ogplay::video::FfmpegAvailable() == available);
}

TEST_CASE("ffmpeg backend decodes the mp4v+aac fixture" *
          doctest::skip(!ogplay::video::FfmpegAvailable())) {
    auto player = ogplay::video::OpenFfmpegVideo(FixturePath());
    const auto& metadata = player->Metadata();
    CHECK(metadata.width == 64U);
    CHECK(metadata.height == 32U);
    CHECK(metadata.duration_ms > 800);
    CHECK(metadata.duration_ms < 1500);
    REQUIRE(metadata.HasAudio());
    CHECK(metadata.audio_channels == 1U);
    CHECK(metadata.audio_sample_rate == 8000U);

    // Fixture is 8 fps for 1 s; walking the clock must deliver each frame
    // exactly once and in order.
    int frames = 0;
    std::int64_t last_pts = -1;
    for (std::int64_t position = 0; position <= metadata.duration_ms + 500;
         position += 25) {
        auto frame = player->TakeFrame(position);
        if (!frame.has_value()) continue;
        ++frames;
        CHECK(frame->width == 64U);
        CHECK(frame->height == 32U);
        CHECK(frame->rgba8.size() == 64U * 32U * 4U);
        CHECK(frame->position_ms > last_pts);
        last_pts = frame->position_ms;
    }
    CHECK(frames == 8);

    std::vector<std::int16_t> pcm(1024U);
    std::size_t total = 0;
    while (true) {
        const std::size_t got = player->ReadPcm(pcm);
        if (got == 0U) break;
        total += got;
    }
    // ~1 s at 8 kHz mono, allow for encoder priming/padding.
    CHECK(total > 6000U);
    CHECK(total < 10000U);
}

TEST_CASE("ffmpeg backend seek rewinds frame delivery" *
          doctest::skip(!ogplay::video::FfmpegAvailable())) {
    auto player = ogplay::video::OpenFfmpegVideo(FixturePath());
    auto late = player->TakeFrame(700);
    REQUIRE(late.has_value());
    CHECK(late->position_ms > 500);
    CHECK_THROWS_AS((void)player->TakeFrame(100),
                    ogplay::video::VideoPlayerError);

    player->SeekTo(0);
    auto rewound = player->TakeFrame(0);
    REQUIRE(rewound.has_value());
    CHECK(rewound->position_ms == 0);

    CHECK_THROWS_AS(player->SeekTo(999999), ogplay::video::VideoPlayerError);
}

TEST_CASE("ffmpeg backend rejects missing and non-video files" *
          doctest::skip(!ogplay::video::FfmpegAvailable())) {
    CHECK_THROWS_AS((void)ogplay::video::OpenFfmpegVideo(
                        std::filesystem::path{"does-not-exist.mp4"}),
                    ogplay::video::VideoPlayerError);
    CHECK_THROWS_AS(
        (void)ogplay::video::OpenFfmpegVideo(std::filesystem::path{
            OGPLAY_SOURCE_DIR} / "tests/fixtures/audio/short-vorbis.ogg"),
        ogplay::video::VideoPlayerError);
}
