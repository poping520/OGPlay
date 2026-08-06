#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ogplay/session/profile_apk.h"

namespace {

constexpr std::string_view kHashA =
    "0000000000000000000000000000000000000000000000000000000000000000";
constexpr std::string_view kHashB =
    "1111111111111111111111111111111111111111111111111111111111111111";

ogplay::session::TitleProfile Profile(const std::string_view hash,
                                      const ogplay::session::ProfileAbi abi) {
    ogplay::session::TitleProfile profile;
    profile.schema = 1;
    profile.identity = {"org.example.legacy", "fixture", {7},
                        {std::string(hash)}, abi};
    profile.runtime = {19, ogplay::session::ProfileLifecycle::gl_surface_view,
                       {640, 360}};
    return profile;
}

ogplay::loader::ApkNativeLibrary Library(
    const std::string_view name, const std::string_view hash,
    const ogplay::loader::AndroidArmAbi abi) {
    return {std::string("lib/") + std::string(ogplay::loader::ToString(abi)) + "/" +
                std::string(name),
            std::string(name), abi, std::string(hash),
            {std::byte{0x7f}, std::byte{'E'}, std::byte{'L'}, std::byte{'F'}}};
}

const ogplay::loader::AndroidManifestFacts kManifest{
    "org.example.legacy", 7, "1.0", 5, std::nullopt};

}  // namespace

TEST_CASE("APK Profile match combines manifest library hash and ABI exactly") {
    const ogplay::session::TitleProfileCatalog profiles(
        {Profile(kHashA, ogplay::session::ProfileAbi::armeabi)});
    const std::vector libraries{
        Library("libother.so", kHashB,
                ogplay::loader::AndroidArmAbi::armeabi_v7a),
        Library("libgame.so", kHashA, ogplay::loader::AndroidArmAbi::armeabi)};

    const auto match =
        ogplay::session::MatchApkTitleProfile(kManifest, libraries, profiles);
    REQUIRE(match.has_value());
    REQUIRE(match->profile != nullptr);
    CHECK(match->profile->identity.package == "org.example.legacy");
    CHECK(match->manifest.version_code == 7);
    CHECK(match->library.basename == "libgame.so");
    CHECK(match->library.sha256 == kHashA);
    CHECK(match->library.image == libraries[1].image);

    const ogplay::session::TitleProfileCatalog no_match(
        {Profile(kHashB, ogplay::session::ProfileAbi::armeabi)});
    CHECK_FALSE(ogplay::session::MatchApkTitleProfile(
                    kManifest, std::span{libraries}.subspan(1), no_match)
                    .has_value());
}

TEST_CASE("APK Profile match rejects ABI lies and ambiguous main libraries") {
    const auto matched =
        Library("libgame.so", kHashA, ogplay::loader::AndroidArmAbi::armeabi);
    const ogplay::session::TitleProfileCatalog wrong_abi(
        {Profile(kHashA, ogplay::session::ProfileAbi::armeabi_v7a)});
    CHECK_THROWS_WITH(static_cast<void>(ogplay::session::MatchApkTitleProfile(
                          kManifest, std::span{&matched, 1}, wrong_abi)),
                      "exact APK profile ABI does not match native library: "
                      "lib/armeabi/libgame.so");

    const ogplay::session::TitleProfileCatalog two_profiles(
        {Profile(kHashA, ogplay::session::ProfileAbi::armeabi),
         Profile(kHashB, ogplay::session::ProfileAbi::armeabi_v7a)});
    const std::vector two_libraries{
        matched,
        Library("libsecond.so", kHashB,
                ogplay::loader::AndroidArmAbi::armeabi_v7a)};
    CHECK_THROWS_WITH(static_cast<void>(ogplay::session::MatchApkTitleProfile(
                          kManifest, two_libraries, two_profiles)),
                      "APK matches multiple profiled native libraries; main library is "
                      "ambiguous");

    CHECK_THROWS_WITH(static_cast<void>(ogplay::session::MatchApkTitleProfile(
                          kManifest, {}, two_profiles)),
                      "APK has no supported ARM native library");
    auto empty = matched;
    empty.image.clear();
    CHECK_THROWS_WITH(static_cast<void>(ogplay::session::MatchApkTitleProfile(
                          kManifest, std::span{&empty, 1}, two_profiles)),
                      "APK native library image is empty: lib/armeabi/libgame.so");
}
