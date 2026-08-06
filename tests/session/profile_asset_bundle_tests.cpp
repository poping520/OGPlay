#include <cstddef>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/session/profile_asset_bundle.h"

namespace {

[[nodiscard]] std::vector<ogplay::session::ProfileVfsMountInput> VfsMounts() {
    return {{"/assets", ogplay::session::ProfileSource::apk,
             {{"data/config.bin", {std::byte{1}}}}}};
}

[[nodiscard]] std::vector<ogplay::session::ProfileAudioResource>
AudioResources() {
    return {{ogplay::session::ProfileSource::apk, "res/raw/music.ogg",
             {std::byte{2}, std::byte{3}}}};
}

}  // namespace

TEST_CASE("Profile asset bundle owns validated imported bytes") {
    auto mounts = VfsMounts();
    auto audio = AudioResources();
    ogplay::session::ProfileAssetBundle bundle{mounts, audio};
    mounts.front().entries.front().contents.clear();
    audio.front().contents.clear();

    REQUIRE(bundle.VfsMounts().size() == 1);
    CHECK(bundle.VfsMounts().front().entries.front().contents.size() == 1);
    REQUIRE(bundle.AudioResources().size() == 1);
    CHECK(bundle.AudioResources().front().contents.size() == 2);

    const ogplay::session::ProfileAssetBundle empty{{}, {}};
    CHECK(empty.VfsMounts().empty());
    CHECK(empty.AudioResources().empty());
}

TEST_CASE("Profile asset bundle rejects ambiguous VFS inputs") {
    auto mounts = VfsMounts();
    mounts.push_back(mounts.front());
    mounts.back().guest = "/ASSETS";
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::ProfileAssetBundle{mounts, {}}),
        ogplay::session::ProfileAssetBundleError);

    mounts = VfsMounts();
    mounts.front().guest = "/assets/../data";
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::ProfileAssetBundle{mounts, {}}),
        ogplay::session::ProfileAssetBundleError);

    mounts = VfsMounts();
    mounts.front().entries.push_back(
        {"DATA/config.bin", {std::byte{2}}});
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::ProfileAssetBundle{mounts, {}}),
        ogplay::session::ProfileAssetBundleError);

    mounts = VfsMounts();
    mounts.front().entries.clear();
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::ProfileAssetBundle{mounts, {}}),
        ogplay::session::ProfileAssetBundleError);
}

TEST_CASE("Profile asset bundle rejects invalid audio inputs") {
    auto audio = AudioResources();
    audio.push_back(audio.front());
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::session::ProfileAssetBundle{{}, audio}),
        "Profile audio asset is duplicated: res/raw/music.ogg",
        ogplay::session::ProfileAssetBundleError);

    audio = AudioResources();
    audio.front().path = "res/../music.ogg";
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::ProfileAssetBundle{{}, audio}),
        ogplay::session::ProfileAssetBundleError);

    audio = AudioResources();
    audio.front().contents.clear();
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::ProfileAssetBundle{{}, audio}),
        ogplay::session::ProfileAssetBundleError);
}
