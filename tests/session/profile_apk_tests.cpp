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

void Put16(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8U);
}

void Put32(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
}

std::vector<std::byte> DynamicElf(const std::string_view soname,
                                  const std::string_view needed = {}) {
    std::vector<std::byte> bytes(0x280, std::byte{});
    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{1};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};
    Put16(bytes, 16, 3);
    Put16(bytes, 18, 40);
    Put32(bytes, 20, 1);
    Put32(bytes, 28, 52);
    Put32(bytes, 36, 0x05000400U);
    Put16(bytes, 40, 52);
    Put16(bytes, 42, 32);
    Put16(bytes, 44, 2);
    Put32(bytes, 52, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 60, 0x10000U);
    Put32(bytes, 68, 0x280);
    Put32(bytes, 72, 0x280);
    Put32(bytes, 76, 5);
    Put32(bytes, 80, 0x1000);
    Put32(bytes, 84, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 88, 0x100);
    Put32(bytes, 92, 0x10100U);
    Put32(bytes, 100, needed.empty() ? 32U : 40U);
    Put32(bytes, 104, needed.empty() ? 32U : 40U);
    Put32(bytes, 108, 6);
    Put32(bytes, 112, 4);
    std::size_t cursor = 1;
    const auto write_string = [&](const std::string_view value) {
        const auto offset = static_cast<std::uint32_t>(cursor);
        for (const char byte : value) {
            bytes[0x200 + cursor++] = static_cast<std::byte>(byte);
        }
        ++cursor;
        return offset;
    };
    const auto soname_offset = write_string(soname);
    const auto needed_offset = needed.empty() ? 0U : write_string(needed);
    Put32(bytes, 0x100, ogplay::loader::kElfDynamicStringTable);
    Put32(bytes, 0x104, 0x10200U);
    Put32(bytes, 0x108, ogplay::loader::kElfDynamicStringTableSize);
    Put32(bytes, 0x10c, static_cast<std::uint32_t>(cursor));
    Put32(bytes, 0x110, ogplay::loader::kElfDynamicSoname);
    Put32(bytes, 0x114, soname_offset);
    std::size_t end = 0x118;
    if (!needed.empty()) {
        Put32(bytes, end, ogplay::loader::kElfDynamicNeeded);
        Put32(bytes, end + 4, needed_offset);
        end += 8;
    }
    Put32(bytes, end, ogplay::loader::kElfDynamicNull);
    return bytes;
}

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

TEST_CASE("APK Profile launch owns the exact root and discovered Bionic closure") {
    const ogplay::session::TitleProfileCatalog profiles(
        {Profile(kHashA, ogplay::session::ProfileAbi::armeabi)});
    auto root = Library("libgame.so", kHashA,
                        ogplay::loader::AndroidArmAbi::armeabi);
    root.image = DynamicElf("libgame.so", "libc.so");
    const auto libc = DynamicElf("libc.so");
    const ogplay::runtime::BionicModuleSource system[]{{"libc.so", libc}};

    const auto launch = ogplay::session::PrepareApkProfileLaunch(
        kManifest, std::span{&root, 1}, profiles, system);
    REQUIRE(launch.has_value());
    CHECK(launch->match.library.basename == "libgame.so");
    CHECK(launch->match.profile->runtime.api_level == 19);
    REQUIRE(launch->modules.Modules().size() == 2);
    CHECK(launch->modules.Modules()[0].name == "libgame.so");
    CHECK(launch->modules.Modules()[1].name == "libc.so");
    CHECK(launch->modules.Modules()[0].image == root.image);
}
