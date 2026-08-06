#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/session/profile_audio.h"

namespace {

class RecordingPlayer final : public ogplay::audio::MusicPlayer {
public:
    void Play(const ogplay::audio::MusicPlaybackRequest& request) override {
        ++calls;
        loop = request.loop;
        bytes.assign(request.encoded.begin(), request.encoded.end());
        if (fail) throw std::runtime_error("player rejected music");
    }

    std::size_t calls{};
    bool loop{};
    bool fail{};
    std::vector<std::byte> bytes;
};

[[nodiscard]] ogplay::session::TitleProfile ProfileWithMusic(
    const ogplay::session::ProfileSource source =
        ogplay::session::ProfileSource::apk) {
    ogplay::session::TitleProfile profile;
    profile.audio = ogplay::session::ProfileAudio{
        ogplay::session::ProfileCoverMusic{source, "res/raw/music.ogg", true}};
    return profile;
}

[[nodiscard]] ogplay::session::ProfileAudioResource MusicResource(
    const ogplay::session::ProfileSource source =
        ogplay::session::ProfileSource::apk) {
    return {source, "res/raw/music.ogg",
            {std::byte{0x4F}, std::byte{0x67}, std::byte{0x67}}};
}

}  // namespace

TEST_CASE("Profile audio applies an exact encoded music resource") {
    const auto profile = ProfileWithMusic();
    const std::vector resources{MusicResource()};
    RecordingPlayer player;

    CHECK(ogplay::session::ApplyProfileAudio(profile, resources, player));
    CHECK(player.calls == 1);
    CHECK(player.loop);
    CHECK(player.bytes == resources.front().contents);
}

TEST_CASE("Profile audio without cover music does not call the player") {
    ogplay::session::TitleProfile profile;
    const std::vector resources{MusicResource()};
    RecordingPlayer player;

    CHECK_FALSE(ogplay::session::ApplyProfileAudio(profile, resources, player));
    profile.audio = ogplay::session::ProfileAudio{};
    CHECK_FALSE(ogplay::session::ApplyProfileAudio(profile, resources, player));
    const std::vector invalid_resources{
        ogplay::session::ProfileAudioResource{}};
    CHECK_FALSE(ogplay::session::ApplyProfileAudio(
        profile, invalid_resources, player));
    CHECK(player.calls == 0);
}

TEST_CASE("Profile audio rejects missing duplicate and empty resources") {
    const auto profile = ProfileWithMusic();
    RecordingPlayer player;
    CHECK_THROWS_WITH_AS(
        static_cast<void>(
            ogplay::session::ApplyProfileAudio(profile, {}, player)),
        "Profile cover music resource is missing: apk:res/raw/music.ogg",
        ogplay::session::ProfileAudioError);

    const std::vector wrong_source{
        MusicResource(ogplay::session::ProfileSource::obb)};
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::ApplyProfileAudio(
                        profile, wrong_source, player)),
                    ogplay::session::ProfileAudioError);

    const std::vector duplicate{MusicResource(), MusicResource()};
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::ApplyProfileAudio(
                        profile, duplicate, player)),
                    ogplay::session::ProfileAudioError);

    auto empty = MusicResource();
    empty.contents.clear();
    const std::vector empty_resources{empty};
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::ApplyProfileAudio(
                        profile, empty_resources, player)),
                    ogplay::session::ProfileAudioError);
}

TEST_CASE("Profile audio propagates player failures") {
    const auto profile = ProfileWithMusic();
    const std::vector resources{MusicResource()};
    RecordingPlayer player;
    player.fail = true;

    CHECK_THROWS_WITH(
        static_cast<void>(
            ogplay::session::ApplyProfileAudio(profile, resources, player)),
        "player rejected music");
    CHECK(player.calls == 1);
}
