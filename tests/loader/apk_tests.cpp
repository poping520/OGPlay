#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "ogplay/loader/apk.h"

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

std::uint32_t Crc32(const std::span<const std::byte> bytes) {
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

std::vector<std::byte> MakeZip(const std::string_view name,
                               const std::span<const std::byte> payload,
                               const std::uint16_t method = 0) {
    const auto crc = Crc32(payload);
    std::vector<std::byte> bytes;
    Append32(bytes, 0x04034b50); Append16(bytes, 20); Append16(bytes, 0);
    Append16(bytes, method); Append16(bytes, 0); Append16(bytes, 0); Append32(bytes, crc);
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size())); Append16(bytes, 0);
    AppendText(bytes, name); bytes.insert(bytes.end(), payload.begin(), payload.end());

    const auto central_offset = static_cast<std::uint32_t>(bytes.size());
    Append32(bytes, 0x02014b50); Append16(bytes, 20); Append16(bytes, 20);
    Append16(bytes, 0); Append16(bytes, method); Append16(bytes, 0); Append16(bytes, 0);
    Append32(bytes, crc); Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size()));
    Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0);
    Append32(bytes, 0); Append32(bytes, 0); AppendText(bytes, name);
    const auto central_size = static_cast<std::uint32_t>(bytes.size()) - central_offset;

    Append32(bytes, 0x06054b50); Append16(bytes, 0); Append16(bytes, 0);
    Append16(bytes, 1); Append16(bytes, 1); Append32(bytes, central_size);
    Append32(bytes, central_offset); Append16(bytes, 0);
    return bytes;
}

}  // namespace

TEST_CASE("APK parser reads an exact stored native library") {
    const std::vector<std::byte> payload{std::byte{0x7f}, std::byte{'E'},
                                         std::byte{'L'}, std::byte{'F'}};
    const auto bytes = MakeZip("lib/armeabi-v7a/libgame.so", payload);
    const auto archive = ogplay::loader::ParseApkArchive(bytes);
    REQUIRE(archive.entries.size() == 1);
    CHECK(archive.entries.front().name == "lib/armeabi-v7a/libgame.so");
    CHECK(ogplay::loader::ReadStoredApkEntry(bytes, archive,
                                             "lib/armeabi-v7a/libgame.so") == payload);
    CHECK_THROWS_WITH(static_cast<void>(
                          ogplay::loader::ReadStoredApkEntry(bytes, archive, "missing.so")),
                      "APK entry was not found");
}

TEST_CASE("APK stored entry rejects corruption and compression") {
    const std::vector<std::byte> payload{std::byte{1}, std::byte{2}, std::byte{3}};
    auto corrupt = MakeZip("lib/armeabi-v7a/libgame.so", payload);
    const auto archive = ogplay::loader::ParseApkArchive(corrupt);
    corrupt[30 + archive.entries.front().name.size()] = std::byte{9};
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ReadStoredApkEntry(
                          corrupt, archive, "lib/armeabi-v7a/libgame.so")),
                      "APK stored entry CRC32 mismatch");

    const auto compressed = MakeZip("classes.dex", payload, 8);
    const auto compressed_archive = ogplay::loader::ParseApkArchive(compressed);
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ReadStoredApkEntry(
                          compressed, compressed_archive, "classes.dex")),
                      "APK entry is not stored without compression");
}

TEST_CASE("APK parser rejects unsafe entry paths") {
    const std::vector<std::byte> payload{std::byte{1}};
    const auto bytes = MakeZip("../libgame.so", payload);
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ParseApkArchive(bytes)),
                      "APK entry name contains an unsafe path segment");
}
