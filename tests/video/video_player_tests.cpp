#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "ogplay/video/fake_video_player.h"
#include "ogplay/video/rgba_canvas.h"
#include "ogplay/video/video_player.h"

namespace {

using ogplay::video::FakeVideoPlayer;
using ogplay::video::VideoMetadata;
using ogplay::video::VideoPlayerError;

[[nodiscard]] VideoMetadata SmallClip() {
    VideoMetadata metadata;
    metadata.width = 8U;
    metadata.height = 4U;
    metadata.duration_ms = 1000;
    metadata.audio_sample_rate = 8000U;
    metadata.audio_channels = 2U;
    return metadata;
}

}  // namespace

TEST_CASE("video metadata validation rejects out-of-bounds facts") {
    CHECK_NOTHROW(ogplay::video::ValidateVideoMetadata(SmallClip()));

    auto zero_width = SmallClip();
    zero_width.width = 0U;
    CHECK_THROWS_AS(ogplay::video::ValidateVideoMetadata(zero_width),
                    VideoPlayerError);

    auto zero_duration = SmallClip();
    zero_duration.duration_ms = 0;
    CHECK_THROWS_AS(ogplay::video::ValidateVideoMetadata(zero_duration),
                    VideoPlayerError);

    auto channels_without_rate = SmallClip();
    channels_without_rate.audio_sample_rate = 0U;
    CHECK_THROWS_AS(
        ogplay::video::ValidateVideoMetadata(channels_without_rate),
        VideoPlayerError);

    auto too_many_channels = SmallClip();
    too_many_channels.audio_channels = 3U;
    CHECK_THROWS_AS(ogplay::video::ValidateVideoMetadata(too_many_channels),
                    VideoPlayerError);

    auto silent = SmallClip();
    silent.audio_sample_rate = 0U;
    silent.audio_channels = 0U;
    CHECK_NOTHROW(ogplay::video::ValidateVideoMetadata(silent));
    CHECK_FALSE(silent.HasAudio());
}

TEST_CASE("fake player delivers each due frame exactly once") {
    FakeVideoPlayer player(SmallClip(), 10U);
    CHECK(player.TotalFrames() == 10);

    auto first = player.TakeFrame(0);
    REQUIRE(first.has_value());
    CHECK(first->position_ms == 0);
    CHECK(first->width == 8U);
    CHECK(first->height == 4U);
    CHECK(first->rgba8.size() == 8U * 4U * 4U);

    CHECK_FALSE(player.TakeFrame(0).has_value());
    CHECK_FALSE(player.TakeFrame(99).has_value());

    auto second = player.TakeFrame(100);
    REQUIRE(second.has_value());
    CHECK(second->position_ms == 100);
}

TEST_CASE("fake player frame pixels encode the frame index") {
    FakeVideoPlayer player(SmallClip(), 10U);
    auto frame = player.TakeFrame(350);
    REQUIRE(frame.has_value());
    const std::uint32_t expected = FakeVideoPlayer::FrameColorRgba(3);
    CHECK(frame->rgba8[0] == static_cast<std::uint8_t>(expected >> 24U));
    CHECK(frame->rgba8[1] == static_cast<std::uint8_t>(expected >> 16U));
    CHECK(frame->rgba8[2] == static_cast<std::uint8_t>(expected >> 8U));
    CHECK(frame->rgba8[3] == 0xFFU);
}

TEST_CASE("fake player skips to the newest due frame when position jumps") {
    FakeVideoPlayer player(SmallClip(), 10U);
    auto frame = player.TakeFrame(520);
    REQUIRE(frame.has_value());
    CHECK(frame->position_ms == 500);
    CHECK_FALSE(player.TakeFrame(590).has_value());
}

TEST_CASE("fake player clamps to the final frame past duration") {
    FakeVideoPlayer player(SmallClip(), 10U);
    auto frame = player.TakeFrame(5000);
    REQUIRE(frame.has_value());
    CHECK(frame->position_ms == 900);
    CHECK_FALSE(player.TakeFrame(6000).has_value());
}

TEST_CASE("fake player rejects backwards position without seek") {
    FakeVideoPlayer player(SmallClip(), 10U);
    CHECK(player.TakeFrame(300).has_value());
    CHECK_THROWS_AS((void)player.TakeFrame(200), VideoPlayerError);
    CHECK_THROWS_AS((void)player.TakeFrame(-1), VideoPlayerError);
}

TEST_CASE("fake player seek moves frame and audio cursors together") {
    FakeVideoPlayer player(SmallClip(), 10U);
    CHECK(player.TakeFrame(900).has_value());

    player.SeekTo(200);
    auto frame = player.TakeFrame(200);
    REQUIRE(frame.has_value());
    CHECK(frame->position_ms == 200);

    std::vector<std::int16_t> pcm(4U);
    CHECK(player.ReadPcm(pcm) == 2U);
    CHECK(pcm[0] == static_cast<std::int16_t>(1600));

    CHECK_THROWS_AS(player.SeekTo(1001), VideoPlayerError);
    CHECK_THROWS_AS(player.SeekTo(-1), VideoPlayerError);
}

TEST_CASE("fake player pcm is a deterministic bounded ramp") {
    FakeVideoPlayer player(SmallClip(), 10U);
    std::vector<std::int16_t> pcm(8U);
    CHECK(player.ReadPcm(pcm) == 4U);
    CHECK(pcm[0] == 0);
    CHECK(pcm[1] == 0);
    CHECK(pcm[2] == 1);
    CHECK(pcm[7] == 3);

    std::vector<std::int16_t> odd(3U);
    CHECK_THROWS_AS((void)player.ReadPcm(odd), VideoPlayerError);

    // 8000 Hz for 1000 ms = 8000 frames total; drain and verify EOF.
    std::vector<std::int16_t> chunk(2048U);
    std::size_t drained = 4U;
    while (true) {
        const std::size_t frames = player.ReadPcm(chunk);
        if (frames == 0U) break;
        drained += frames;
    }
    CHECK(drained == 8000U);
    CHECK(player.ReadPcm(chunk) == 0U);
}

TEST_CASE("fake player without audio returns zero pcm frames") {
    auto metadata = SmallClip();
    metadata.audio_sample_rate = 0U;
    metadata.audio_channels = 0U;
    FakeVideoPlayer player(metadata, 10U);
    std::vector<std::int16_t> pcm(8U);
    CHECK(player.ReadPcm(pcm) == 0U);
}

TEST_CASE("rgba canvas letterboxes with centered black bars") {
    ogplay::video::VideoFrame frame;
    frame.width = 2U;
    frame.height = 2U;
    frame.rgba8 = {
        // 2x2 all-red source
        255, 0, 0, 255, 255, 0, 0, 255,
        255, 0, 0, 255, 255, 0, 0, 255,
    };
    // 8x4 canvas: 2x2 source scales to 4x4? No - fit by height gives 4x4,
    // centered horizontally at x=2..5 with black pillars either side.
    const auto canvas = ogplay::video::ComposeRgbaOnCanvas(frame, 8U, 4U);
    REQUIRE(canvas.size() == 8U * 4U * 4U);
    const auto pixel = [&](std::size_t x, std::size_t y) {
        using PixelDifference =
            std::vector<std::uint8_t>::difference_type;
        const auto offset =
            static_cast<PixelDifference>((y * 8U + x) * 4U);
        const auto begin = canvas.cbegin() + offset;
        return std::vector<std::uint8_t>(begin, begin + 4);
    };
    CHECK(pixel(0, 0) == std::vector<std::uint8_t>{0, 0, 0, 255});
    CHECK(pixel(1, 1) == std::vector<std::uint8_t>{0, 0, 0, 255});
    CHECK(pixel(2, 0) == std::vector<std::uint8_t>{255, 0, 0, 255});
    CHECK(pixel(5, 3) == std::vector<std::uint8_t>{255, 0, 0, 255});
    CHECK(pixel(6, 2) == std::vector<std::uint8_t>{0, 0, 0, 255});
    CHECK(pixel(7, 3) == std::vector<std::uint8_t>{0, 0, 0, 255});
}

TEST_CASE("rgba canvas passes same-size frames through unchanged") {
    ogplay::video::VideoFrame frame;
    frame.width = 3U;
    frame.height = 2U;
    frame.rgba8.assign(3U * 2U * 4U, 0x7FU);
    const auto canvas = ogplay::video::ComposeRgbaOnCanvas(frame, 3U, 2U);
    CHECK(canvas == frame.rgba8);
}

TEST_CASE("rgba canvas rejects malformed input") {
    ogplay::video::VideoFrame frame;
    frame.width = 2U;
    frame.height = 2U;
    frame.rgba8.assign(3U, 0U);  // wrong buffer size
    CHECK_THROWS_AS(
        (void)ogplay::video::ComposeRgbaOnCanvas(frame, 8U, 4U),
        VideoPlayerError);
    frame.rgba8.assign(2U * 2U * 4U, 0U);
    CHECK_THROWS_AS((void)ogplay::video::ComposeRgbaOnCanvas(frame, 0U, 4U),
                    VideoPlayerError);
    CHECK_THROWS_AS(
        (void)ogplay::video::ComposeRgbaOnCanvas(frame, 8U, 5000U),
        VideoPlayerError);
}

TEST_CASE("fake player rejects out-of-bounds construction") {
    CHECK_THROWS_AS(FakeVideoPlayer(SmallClip(), 0U), VideoPlayerError);
    CHECK_THROWS_AS(FakeVideoPlayer(SmallClip(), 121U), VideoPlayerError);
    auto bad = SmallClip();
    bad.height = 5000U;
    CHECK_THROWS_AS(FakeVideoPlayer(bad, 10U), VideoPlayerError);
}
