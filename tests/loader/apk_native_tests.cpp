#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/loader/apk.h"
#include "ogplay/loader/apk_native.h"

namespace {

void Append16(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void Append32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

void AppendText(std::vector<std::byte>& bytes, const std::string_view text) {
    for (const auto value : text) bytes.push_back(static_cast<std::byte>(value));
}

std::uint32_t Crc32(const std::vector<std::byte>& bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const auto mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

std::vector<std::byte> Bytes(const std::string_view text) {
    std::vector<std::byte> result;
    for (const auto value : text) result.push_back(static_cast<std::byte>(value));
    return result;
}

struct Input final {
    std::string name;
    std::vector<std::byte> payload;
};

struct Central final {
    const Input* input{};
    std::uint32_t crc{};
    std::uint32_t local_offset{};
};

std::vector<std::byte> MakeApk(const std::vector<Input>& inputs) {
    std::vector<std::byte> bytes;
    std::vector<Central> central;
    for (const auto& input : inputs) {
        const auto offset = static_cast<std::uint32_t>(bytes.size());
        const auto crc = Crc32(input.payload);
        Append32(bytes, 0x04034b50); Append16(bytes, 20); Append16(bytes, 0);
        Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0); Append32(bytes, crc);
        Append32(bytes, static_cast<std::uint32_t>(input.payload.size()));
        Append32(bytes, static_cast<std::uint32_t>(input.payload.size()));
        Append16(bytes, static_cast<std::uint16_t>(input.name.size())); Append16(bytes, 0);
        AppendText(bytes, input.name);
        bytes.insert(bytes.end(), input.payload.begin(), input.payload.end());
        central.push_back({&input, crc, offset});
    }
    const auto central_offset = static_cast<std::uint32_t>(bytes.size());
    for (const auto& entry : central) {
        Append32(bytes, 0x02014b50); Append16(bytes, 20); Append16(bytes, 20);
        Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0);
        Append32(bytes, entry.crc);
        Append32(bytes, static_cast<std::uint32_t>(entry.input->payload.size()));
        Append32(bytes, static_cast<std::uint32_t>(entry.input->payload.size()));
        Append16(bytes, static_cast<std::uint16_t>(entry.input->name.size()));
        Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0);
        Append32(bytes, 0); Append32(bytes, entry.local_offset);
        AppendText(bytes, entry.input->name);
    }
    const auto central_size = static_cast<std::uint32_t>(bytes.size()) - central_offset;
    Append32(bytes, 0x06054b50); Append16(bytes, 0); Append16(bytes, 0);
    Append16(bytes, static_cast<std::uint16_t>(central.size()));
    Append16(bytes, static_cast<std::uint16_t>(central.size()));
    Append32(bytes, central_size); Append32(bytes, central_offset); Append16(bytes, 0);
    return bytes;
}

ogplay::loader::ApkNativeLibraryInventory Inventory(
    const std::vector<Input>& inputs) {
    const auto apk = MakeApk(inputs);
    const auto archive = ogplay::loader::ParseApkArchive(apk);
    return ogplay::loader::ReadApkNativeLibraryInventory(apk, archive);
}

}  // namespace

TEST_CASE("APK native catalog owns exact ARM library bytes and SHA-256") {
    const auto abc = Bytes("abc");
    const auto hello = Bytes("hello");
    const std::vector<Input> inputs{
        {"lib/armeabi/libold.so", abc},
        {"assets/fake.so", Bytes("ignored")},
        {"lib/x86/libignored.so", Bytes("ignored")},
        {"lib/armeabi-v7a/libgame.so", hello},
        {"lib/armeabi/nested/libbad.so", Bytes("ignored")}};
    const auto apk = MakeApk(inputs);
    const auto archive = ogplay::loader::ParseApkArchive(apk);
    const auto libraries = ogplay::loader::ReadApkArmNativeLibraries(apk, archive);
    REQUIRE(libraries.size() == 2);
    CHECK(libraries[0].entry_name == "lib/armeabi-v7a/libgame.so");
    CHECK(libraries[0].basename == "libgame.so");
    CHECK(libraries[0].abi == ogplay::loader::AndroidArmAbi::armeabi_v7a);
    CHECK(libraries[0].sha256 ==
          "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
    CHECK(libraries[0].image == hello);
    CHECK(libraries[1].entry_name == "lib/armeabi/libold.so");
    CHECK(libraries[1].abi == ogplay::loader::AndroidArmAbi::armeabi);
    CHECK(libraries[1].sha256 ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(libraries[1].image == abc);
}

TEST_CASE("APK native catalog rejects empty libraries and never selects one") {
    const auto empty_apk = MakeApk({{"lib/armeabi/libempty.so", {}}});
    const auto empty_archive = ogplay::loader::ParseApkArchive(empty_apk);
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ReadApkArmNativeLibraries(
                          empty_apk, empty_archive)),
                      "APK native library is empty: lib/armeabi/libempty.so");

    const auto no_arm = MakeApk({{"lib/x86/libgame.so", Bytes("x")}});
    const auto no_arm_archive = ogplay::loader::ParseApkArchive(no_arm);
    CHECK(ogplay::loader::ReadApkArmNativeLibraries(no_arm, no_arm_archive).empty());
    CHECK(ogplay::loader::ToString(ogplay::loader::AndroidArmAbi::armeabi) == "armeabi");
    CHECK(ogplay::loader::ToString(ogplay::loader::AndroidArmAbi::armeabi_v7a) ==
          "armeabi-v7a");
}

TEST_CASE("APK native inventory records soname logical name and ABI facts") {
    const auto inventory = Inventory({
        {"lib/armeabi/libold.so", Bytes("old")},
        {"lib/armeabi-v7a/libgame.so", Bytes("game")},
        {"lib/armeabi-v7a/plugin.so", Bytes("plugin")},
    });
    CHECK_FALSE(inventory.Empty());
    CHECK(inventory.HasAbi(ogplay::loader::AndroidArmAbi::armeabi));
    CHECK(inventory.HasAbi(ogplay::loader::AndroidArmAbi::armeabi_v7a));
    CHECK(inventory.Abis() ==
          std::vector{ogplay::loader::AndroidArmAbi::armeabi_v7a,
                      ogplay::loader::AndroidArmAbi::armeabi});
    const auto* game = inventory.FindSoname(
        ogplay::loader::AndroidArmAbi::armeabi_v7a, "libgame.so");
    REQUIRE(game != nullptr);
    CHECK(game->logical_name == "game");
    CHECK(inventory.FindLogicalName(
              ogplay::loader::AndroidArmAbi::armeabi_v7a, "plugin") == nullptr);
}

TEST_CASE("APK native inventory rejects forged and duplicate lookup identities") {
    using ogplay::loader::AndroidArmAbi;
    using ogplay::loader::ApkNativeInventoryError;
    using ogplay::loader::ApkNativeLibrary;
    SUBCASE("logical name disagrees with the entry basename") {
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::loader::ApkNativeLibraryInventory({
                ApkNativeLibrary{"lib/armeabi/libgame.so", "libgame.so",
                                 AndroidArmAbi::armeabi, "ignored", Bytes("x"),
                                 "libgame.so", "other"},
            })),
            ApkNativeInventoryError);
    }
    SUBCASE("same ABI contains duplicate soname") {
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::loader::ApkNativeLibraryInventory({
                ApkNativeLibrary{"first", "libgame.so", AndroidArmAbi::armeabi,
                                 "ignored", Bytes("x")},
                ApkNativeLibrary{"second", "libgame.so", AndroidArmAbi::armeabi,
                                 "ignored", Bytes("y")},
            })),
            ApkNativeInventoryError);
    }
}

TEST_CASE("APK process ABI resolver covers single and dual ABI inventories") {
    const auto armeabi = Inventory({{"lib/armeabi/libgame.so", Bytes("a")}});
    CHECK(ogplay::loader::ResolveApkProcessAbi(armeabi) ==
          ogplay::loader::AndroidArmAbi::armeabi);

    const auto v7a = Inventory({{"lib/armeabi-v7a/libgame.so", Bytes("v7")}});
    CHECK(ogplay::loader::ResolveApkProcessAbi(v7a) ==
          ogplay::loader::AndroidArmAbi::armeabi_v7a);

    const auto dual = Inventory({
        {"lib/armeabi/libgame.so", Bytes("a")},
        {"lib/armeabi-v7a/libgame.so", Bytes("v7")},
    });
    CHECK(ogplay::loader::ResolveApkProcessAbi(dual) ==
          ogplay::loader::AndroidArmAbi::armeabi_v7a);
    constexpr std::array reversed{
        ogplay::loader::AndroidArmAbi::armeabi,
        ogplay::loader::AndroidArmAbi::armeabi_v7a,
    };
    CHECK(ogplay::loader::ResolveApkProcessAbi(reversed, dual) ==
          ogplay::loader::AndroidArmAbi::armeabi);
}

TEST_CASE("selected native library view cannot cross the process ABI") {
    const auto inventory = Inventory({
        {"lib/armeabi/libgame.so", Bytes("old")},
        {"lib/armeabi-v7a/libgame.so", Bytes("new")},
    });
    const auto abi = ogplay::loader::ResolveApkProcessAbi(inventory);
    const auto selected = ogplay::loader::SelectApkNativeLibraries(inventory, abi);
    CHECK(selected.Abi() == ogplay::loader::AndroidArmAbi::armeabi_v7a);
    const auto* game = selected.FindLogicalName("game");
    REQUIRE(game != nullptr);
    CHECK(game->entry_name == "lib/armeabi-v7a/libgame.so");
    CHECK(game->image == Bytes("new"));
    CHECK(selected.FindSoname("libmissing.so") == nullptr);
}

TEST_CASE("APK process ABI resolver reports empty and disjoint inventories") {
    const ogplay::loader::ApkNativeLibraryInventory empty{
        std::vector<ogplay::loader::ApkNativeLibrary>{}};
    try {
        static_cast<void>(ogplay::loader::ResolveApkProcessAbi(empty));
        FAIL("expected pure Java ABI limitation");
    } catch (const ogplay::loader::ApkNativeInventoryError& error) {
        CHECK(error.Reason() ==
              ogplay::loader::ApkNativeInventoryErrorReason::native_abi_required);
    }

    const auto v7a = Inventory({{"lib/armeabi-v7a/libgame.so", Bytes("v7")}});
    constexpr std::array only_armeabi{ogplay::loader::AndroidArmAbi::armeabi};
    try {
        static_cast<void>(ogplay::loader::ResolveApkProcessAbi(only_armeabi, v7a));
        FAIL("expected incompatible ABI error");
    } catch (const ogplay::loader::ApkNativeInventoryError& error) {
        CHECK(error.Reason() ==
              ogplay::loader::ApkNativeInventoryErrorReason::no_compatible_abi);
    }
}

TEST_CASE("APK process ABI resolver does not inspect profile hash facts") {
    const auto apk = MakeApk({{"lib/armeabi/libgame.so", Bytes("a")}});
    const auto archive = ogplay::loader::ParseApkArchive(apk);
    auto libraries = ogplay::loader::ReadApkArmNativeLibraries(apk, archive);
    REQUIRE(libraries.size() == 1);
    libraries[0].sha256 = "not-a-profile-fingerprint";
    const ogplay::loader::ApkNativeLibraryInventory inventory{
        std::move(libraries)};
    CHECK(ogplay::loader::ResolveApkProcessAbi(inventory) ==
          ogplay::loader::AndroidArmAbi::armeabi);
}
