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

ogplay::session::TitleProfile Profile(const std::string_view hash) {
    ogplay::session::TitleProfile profile;
    profile.schema = 4;
    profile.identity = {"org.example.legacy", {7}, {std::string(hash)}};
    return profile;
}

ogplay::session::TitleProfile PackageProfile() {
    ogplay::session::TitleProfile profile;
    profile.schema = 4;
    profile.identity.package = "org.example.legacy";
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
    "org.example.legacy", 7, "1.0", 5, std::nullopt, std::nullopt,
    std::nullopt, std::nullopt};

}  // namespace

TEST_CASE("compatibility profile selection treats no match as generic startup") {
    const ogplay::session::TitleProfileCatalog empty({});
    const auto none = ogplay::session::SelectApkCompatibilityProfile(
        kManifest, {}, empty);
    CHECK(none.profile == nullptr);
    CHECK(none.applicability_notes ==
          std::vector<std::string>{"generic startup",
                                   "no applicable compatibility profile"});

    const auto library =
        Library("libgame.so", kHashA, ogplay::loader::AndroidArmAbi::armeabi);
    const ogplay::session::TitleProfileCatalog exact(
        {Profile(kHashA)});
    CHECK(ogplay::session::SelectApkCompatibilityProfile(
              kManifest, std::span{&library, 1}, exact)
              .profile != nullptr);

    const ogplay::session::TitleProfileCatalog mismatch(
        {Profile(kHashB)});
    CHECK(ogplay::session::SelectApkCompatibilityProfile(
              kManifest, std::span{&library, 1}, mismatch)
              .profile == nullptr);

    const ogplay::session::TitleProfileCatalog optional({PackageProfile()});
    const auto selected = ogplay::session::SelectApkCompatibilityProfile(
        kManifest, {}, optional);
    REQUIRE(selected.profile != nullptr);
    CHECK(selected.profile->schema == 4U);
    CHECK(selected.profile->identity.package == "org.example.legacy");
}

TEST_CASE("compatibility profile version guard and ABI independence") {
    const ogplay::session::TitleProfileCatalog profiles({Profile(kHashA)});
    for (const auto abi : {ogplay::loader::AndroidArmAbi::armeabi,
                           ogplay::loader::AndroidArmAbi::armeabi_v7a}) {
        const auto library = Library("libgame.so", kHashA, abi);
        CHECK(ogplay::session::SelectApkCompatibilityProfile(
            kManifest, std::span{&library, 1}, profiles).profile != nullptr);
        auto other_version = kManifest;
        other_version.version_code = 8;
        CHECK(ogplay::session::SelectApkCompatibilityProfile(
            other_version, std::span{&library, 1}, profiles).profile == nullptr);
    }
}
