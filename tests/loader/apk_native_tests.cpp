#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
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
