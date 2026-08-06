#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
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
                               const std::uint16_t method = 0,
                               const std::span<const std::byte> encoded = {},
                               const bool descriptor = false,
                               const bool descriptor_signature = true) {
    const auto crc = Crc32(payload);
    const auto body = method == 0 ? payload : encoded;
    const std::uint16_t flags = descriptor ? 8 : 0;
    std::vector<std::byte> bytes;
    Append32(bytes, 0x04034b50); Append16(bytes, 20); Append16(bytes, flags);
    Append16(bytes, method); Append16(bytes, 0); Append16(bytes, 0); Append32(bytes, crc);
    if (descriptor) {
        bytes.resize(bytes.size() - 4);
        Append32(bytes, 0); Append32(bytes, 0); Append32(bytes, 0);
    } else {
        Append32(bytes, static_cast<std::uint32_t>(body.size()));
        Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    }
    Append16(bytes, static_cast<std::uint16_t>(name.size())); Append16(bytes, 0);
    AppendText(bytes, name); bytes.insert(bytes.end(), body.begin(), body.end());
    if (descriptor) {
        if (descriptor_signature) Append32(bytes, 0x08074b50);
        Append32(bytes, crc);
        Append32(bytes, static_cast<std::uint32_t>(body.size()));
        Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    }

    const auto central_offset = static_cast<std::uint32_t>(bytes.size());
    Append32(bytes, 0x02014b50); Append16(bytes, 20); Append16(bytes, 20);
    Append16(bytes, flags); Append16(bytes, method); Append16(bytes, 0); Append16(bytes, 0);
    Append32(bytes, crc); Append32(bytes, static_cast<std::uint32_t>(body.size()));
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

std::vector<std::byte> Bytes(const std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const auto value : values) result.push_back(static_cast<std::byte>(value));
    return result;
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
    CHECK(ogplay::loader::ReadApkEntry(bytes, archive,
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

    const auto compressed = MakeZip("classes.dex", payload, 12, payload);
    const auto compressed_archive = ogplay::loader::ParseApkArchive(compressed);
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ReadStoredApkEntry(
                          compressed, compressed_archive, "classes.dex")),
                      "APK entry is not stored without compression");
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ReadApkEntry(
                          compressed, compressed_archive, "classes.dex")),
                      "APK entry uses an unsupported compression method");
}

TEST_CASE("APK reader inflates fixed and dynamic Deflate entries") {
    const std::vector<std::byte> stored_payload{
        std::byte{1}, std::byte{2}, std::byte{3}};
    const auto stored_encoded = Bytes({0x01, 0x03, 0x00, 0xfc, 0xff, 0x01, 0x02, 0x03});
    const auto stored = MakeZip("stored.bin", stored_payload, 8, stored_encoded);
    const auto stored_archive = ogplay::loader::ParseApkArchive(stored);
    CHECK(ogplay::loader::ReadApkEntry(stored, stored_archive, "stored.bin") ==
          stored_payload);

    const std::string_view fixed_text = "hello deflated apk entry";
    std::vector<std::byte> fixed_payload;
    for (const auto value : fixed_text) fixed_payload.push_back(static_cast<std::byte>(value));
    const auto fixed_encoded = Bytes({
        0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0x48, 0x49, 0x4d, 0xcb, 0x49, 0x2c, 0x49,
        0x4d, 0x51, 0x48, 0x2c, 0xc8, 0x56, 0x48, 0xcd, 0x2b, 0x29, 0xaa, 0x04, 0x00});
    const auto fixed = MakeZip("classes.dex", fixed_payload, 8, fixed_encoded);
    const auto fixed_archive = ogplay::loader::ParseApkArchive(fixed);
    CHECK(ogplay::loader::ReadApkEntry(fixed, fixed_archive, "classes.dex") == fixed_payload);

    std::vector<std::byte> dynamic_payload(4000, std::byte{'A'});
    for (std::size_t repeat = 0; repeat < 1000; ++repeat) {
        for (const auto value : std::string_view{"BCDE"}) {
            dynamic_payload.push_back(static_cast<std::byte>(value));
        }
    }
    for (std::size_t repeat = 0; repeat < 300; ++repeat) {
        for (const auto value : std::string_view{"xyz"}) {
            dynamic_payload.push_back(static_cast<std::byte>(value));
        }
    }
    const auto dynamic_encoded = Bytes({
        0xed, 0xc3, 0x41, 0x11, 0x00, 0x20, 0x0c, 0x04, 0x31, 0x6d,
        0x85, 0xd6, 0x13, 0xa0, 0x1e, 0x19, 0xf7, 0x49, 0x66, 0xb7,
        0x0a, 0x00, 0x00, 0x00, 0x48, 0x5b, 0xbb, 0xc7, 0xb6, 0x6d,
        0xdb, 0xb6, 0x6d, 0x67, 0x3f, 0xf7, 0x49, 0x0a, 0xf6, 0x01});
    const auto dynamic = MakeZip("assets/data.bar", dynamic_payload, 8, dynamic_encoded);
    const auto dynamic_archive = ogplay::loader::ParseApkArchive(dynamic);
    CHECK(ogplay::loader::ReadApkEntry(dynamic, dynamic_archive, "assets/data.bar") ==
          dynamic_payload);

    const auto described = MakeZip("lib/armeabi/libgame.so", dynamic_payload, 8,
                                   dynamic_encoded, true);
    const auto described_archive = ogplay::loader::ParseApkArchive(described);
    CHECK(described_archive.entries.front().general_purpose_flags == 8);
    CHECK(ogplay::loader::ReadApkEntry(described, described_archive,
                                      "lib/armeabi/libgame.so") == dynamic_payload);
    const auto unsigned_descriptor = MakeZip("unsigned.bin", stored_payload, 8,
                                             stored_encoded, true, false);
    const auto unsigned_archive = ogplay::loader::ParseApkArchive(unsigned_descriptor);
    CHECK(ogplay::loader::ReadApkEntry(unsigned_descriptor, unsigned_archive,
                                      "unsigned.bin") == stored_payload);
}

TEST_CASE("APK Deflate reader rejects malformed streams and metadata") {
    const std::vector<std::byte> payload{std::byte{'A'}};
    const auto reserved = Bytes({0x07});
    const auto bad_stream = MakeZip("bad.bin", payload, 8, reserved);
    const auto bad_archive = ogplay::loader::ParseApkArchive(bad_stream);
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ReadApkEntry(
                          bad_stream, bad_archive, "bad.bin")),
                      "APK deflate block type is reserved");

    const auto wrong_payload_stream = Bytes({0x73, 0x02, 0x00});
    const auto bad_crc = MakeZip("bad-crc.bin", payload, 8, wrong_payload_stream);
    const auto bad_crc_archive = ogplay::loader::ParseApkArchive(bad_crc);
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ReadApkEntry(
                          bad_crc, bad_crc_archive, "bad-crc.bin")),
                      "APK deflated entry CRC32 mismatch");

    const auto one_byte_stream = Bytes({0x73, 0x04, 0x00});
    const std::vector<std::byte> two_bytes{std::byte{'A'}, std::byte{'A'}};
    const auto bad_size = MakeZip("bad-size.bin", two_bytes, 8, one_byte_stream);
    const auto bad_size_archive = ogplay::loader::ParseApkArchive(bad_size);
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ReadApkEntry(
                          bad_size, bad_size_archive, "bad-size.bin")),
                      "APK deflate output size disagrees with entry metadata");

    auto bad_descriptor = MakeZip("bad-descriptor.bin", payload, 8,
                                  one_byte_stream, true);
    const auto bad_descriptor_archive = ogplay::loader::ParseApkArchive(bad_descriptor);
    const auto descriptor_offset = 30U + bad_descriptor_archive.entries.front().name.size() +
                                   one_byte_stream.size();
    bad_descriptor[descriptor_offset + 4] ^= std::byte{1};
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ReadApkEntry(
                          bad_descriptor, bad_descriptor_archive, "bad-descriptor.bin")),
                      "APK data descriptor disagrees with central metadata");
}

TEST_CASE("APK parser rejects unsafe entry paths") {
    const std::vector<std::byte> payload{std::byte{1}};
    const auto bytes = MakeZip("../libgame.so", payload);
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ParseApkArchive(bytes)),
                      "APK entry name contains an unsafe path segment");
}
