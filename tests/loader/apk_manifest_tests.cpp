#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/loader/apk_manifest.h"

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

void Set32(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

void AppendAscii(std::vector<std::byte>& bytes, const std::string_view text) {
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

std::vector<std::byte> StoredApk(const std::string_view name,
                                 const std::vector<std::byte>& payload) {
    const auto crc = Crc32(payload);
    std::vector<std::byte> bytes;
    Append32(bytes, 0x04034b50); Append16(bytes, 20); Append16(bytes, 0);
    Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0); Append32(bytes, crc);
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size())); Append16(bytes, 0);
    AppendAscii(bytes, name); bytes.insert(bytes.end(), payload.begin(), payload.end());
    const auto central_offset = static_cast<std::uint32_t>(bytes.size());
    Append32(bytes, 0x02014b50); Append16(bytes, 20); Append16(bytes, 20);
    Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0);
    Append32(bytes, crc); Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size()));
    Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0);
    Append32(bytes, 0); Append32(bytes, 0); AppendAscii(bytes, name);
    const auto central_size = static_cast<std::uint32_t>(bytes.size()) - central_offset;
    Append32(bytes, 0x06054b50); Append16(bytes, 0); Append16(bytes, 0);
    Append16(bytes, 1); Append16(bytes, 1); Append32(bytes, central_size);
    Append32(bytes, central_offset); Append16(bytes, 0);
    return bytes;
}

std::vector<std::byte> StringPool(const std::vector<std::string>& strings,
                                  const bool utf8) {
    std::vector<std::byte> data;
    std::vector<std::uint32_t> offsets;
    for (const auto& string : strings) {
        offsets.push_back(static_cast<std::uint32_t>(data.size()));
        if (utf8) {
            data.push_back(static_cast<std::byte>(string.size()));
            data.push_back(static_cast<std::byte>(string.size()));
            AppendAscii(data, string);
            data.push_back(std::byte{0});
        } else {
            Append16(data, static_cast<std::uint16_t>(string.size()));
            for (const auto value : string) Append16(data, static_cast<std::uint8_t>(value));
            Append16(data, 0);
        }
    }
    while (data.size() % 4U != 0) data.push_back(std::byte{0});

    std::vector<std::byte> chunk;
    Append16(chunk, 0x0001); Append16(chunk, 28); Append32(chunk, 0);
    Append32(chunk, static_cast<std::uint32_t>(strings.size()));
    Append32(chunk, 0); Append32(chunk, utf8 ? 0x100U : 0U);
    Append32(chunk, static_cast<std::uint32_t>(28U + offsets.size() * 4U));
    Append32(chunk, 0);
    for (const auto offset : offsets) Append32(chunk, offset);
    chunk.insert(chunk.end(), data.begin(), data.end());
    Set32(chunk, 4, static_cast<std::uint32_t>(chunk.size()));
    return chunk;
}

struct Attribute final {
    std::uint32_t name{};
    std::uint32_t raw_value{0xffffffffU};
    std::uint8_t type{};
    std::uint32_t data{};
    std::uint32_t namespace_index{0xffffffffU};
};

std::vector<std::byte> StartElement(const std::uint32_t name,
                                    const std::vector<Attribute>& attributes) {
    std::vector<std::byte> chunk;
    Append16(chunk, 0x0102); Append16(chunk, 16);
    Append32(chunk, static_cast<std::uint32_t>(36U + attributes.size() * 20U));
    Append32(chunk, 1); Append32(chunk, 0xffffffffU);
    Append32(chunk, 0xffffffffU); Append32(chunk, name);
    Append16(chunk, 20); Append16(chunk, 20);
    Append16(chunk, static_cast<std::uint16_t>(attributes.size()));
    Append16(chunk, 0); Append16(chunk, 0); Append16(chunk, 0);
    for (const auto& attribute : attributes) {
        Append32(chunk, attribute.namespace_index); Append32(chunk, attribute.name);
        Append32(chunk, attribute.raw_value); Append16(chunk, 8);
        chunk.push_back(std::byte{0}); chunk.push_back(static_cast<std::byte>(attribute.type));
        Append32(chunk, attribute.data);
    }
    return chunk;
}

std::vector<std::byte> EndElement(const std::uint32_t name) {
    std::vector<std::byte> chunk;
    Append16(chunk, 0x0103); Append16(chunk, 16); Append32(chunk, 24);
    Append32(chunk, 1); Append32(chunk, 0xffffffffU);
    Append32(chunk, 0xffffffffU); Append32(chunk, name);
    return chunk;
}

void Append(std::vector<std::byte>& destination, const std::vector<std::byte>& source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

std::vector<std::byte> Manifest(const bool utf8 = false,
                                const std::string_view package = "org.example.game",
                                const std::uint8_t version_type = 0x10,
                                const std::vector<Attribute>& application_attributes = {}) {
    const std::vector<std::string> strings{
        "manifest", "package", "versionCode", "versionName", "uses-sdk",
        "minSdkVersion", "targetSdkVersion", std::string(package), "1.2.3",
        "http://schemas.android.com/apk/res/android", "application", "icon",
        "label", "OGPlay Game"};
    std::vector<std::byte> result;
    Append16(result, 0x0003); Append16(result, 8); Append32(result, 0);
    Append(result, StringPool(strings, utf8));
    Append(result, StartElement(0, {{1, 7, 0x03, 7},
                                    {2, 0xffffffffU, version_type, 7, 9},
                                    {3, 8, 0x03, 8, 9}}));
    Append(result, StartElement(4, {{5, 0xffffffffU, 0x10, 5, 9},
                                    {6, 0xffffffffU, 0x10, 19, 9}}));
    Append(result, EndElement(4));
    Append(result, StartElement(10, application_attributes));
    Append(result, EndElement(10));
    Append(result, EndElement(0));
    Set32(result, 4, static_cast<std::uint32_t>(result.size()));
    return result;
}

}  // namespace

TEST_CASE("binary AndroidManifest exposes exact package version and SDK facts") {
    const auto facts = ogplay::loader::ParseAndroidBinaryManifest(Manifest());
    CHECK(facts.package == "org.example.game");
    CHECK(facts.version_code == 7);
    CHECK(facts.version_name == "1.2.3");
    CHECK(facts.min_sdk == 5);
    CHECK(facts.target_sdk == 19);
    CHECK_FALSE(facts.application_icon.has_value());
    CHECK_FALSE(facts.application_label.has_value());

    const auto utf8 = ogplay::loader::ParseAndroidBinaryManifest(Manifest(true));
    CHECK(utf8.package == facts.package);
    CHECK(utf8.version_code == facts.version_code);

    const auto apk = StoredApk("AndroidManifest.xml", Manifest());
    const auto archive = ogplay::loader::ParseApkArchive(apk);
    const auto imported = ogplay::loader::ReadAndroidManifest(apk, archive);
    CHECK(imported.package == facts.package);
    CHECK(imported.version_name == facts.version_name);
}

TEST_CASE("binary AndroidManifest distinguishes application visual attributes") {
    const auto referenced = ogplay::loader::ParseAndroidBinaryManifest(Manifest(
        false, "org.example.game", 0x10,
        {{11, 0xffffffffU, 0x01, 0x7f020001U, 9},
         {12, 0xffffffffU, 0x01, 0x7f030002U, 9}}));
    CHECK(referenced.application_icon == 0x7f020001U);
    REQUIRE(referenced.application_label.has_value());
    REQUIRE(std::holds_alternative<std::uint32_t>(*referenced.application_label));
    CHECK(std::get<std::uint32_t>(*referenced.application_label) == 0x7f030002U);

    const auto literal = ogplay::loader::ParseAndroidBinaryManifest(Manifest(
        false, "org.example.game", 0x10,
        {{12, 13, 0x03, 13, 9}}));
    REQUIRE(literal.application_label.has_value());
    REQUIRE(std::holds_alternative<std::string>(*literal.application_label));
    CHECK(std::get<std::string>(*literal.application_label) == "OGPlay Game");
}

TEST_CASE("binary AndroidManifest rejects invalid application visual types") {
    CHECK_THROWS_WITH(
        static_cast<void>(ogplay::loader::ParseAndroidBinaryManifest(Manifest(
            false, "org.example.game", 0x10,
            {{11, 13, 0x03, 13, 9}}))),
        "binary AndroidManifest attribute application icon is not a resource reference");
    CHECK_THROWS_WITH(
        static_cast<void>(ogplay::loader::ParseAndroidBinaryManifest(Manifest(
            false, "org.example.game", 0x10,
            {{12, 0xffffffffU, 0x10, 7, 9}}))),
        "binary AndroidManifest application label is neither a resource reference nor a string");
}

TEST_CASE("binary AndroidManifest rejects identity type and structure errors") {
    CHECK_THROWS_WITH(
        static_cast<void>(ogplay::loader::ParseAndroidBinaryManifest(
            Manifest(false, "invalid"))),
        "binary AndroidManifest identity or structure is invalid");
    CHECK_THROWS_WITH(
        static_cast<void>(ogplay::loader::ParseAndroidBinaryManifest(
            Manifest(false, "org.example.game", 0x03))),
        "binary AndroidManifest attribute versionCode is not an integer");

    auto mismatched_end = Manifest();
    mismatched_end[mismatched_end.size() - 4] = std::byte{4};
    CHECK_THROWS_WITH(
        static_cast<void>(ogplay::loader::ParseAndroidBinaryManifest(mismatched_end)),
        "binary XML element nesting is invalid");
}

TEST_CASE("binary AndroidManifest rejects hostile string pool offsets") {
    auto invalid_offset = Manifest();
    Set32(invalid_offset, 8 + 28, 0xffffffffU);
    CHECK_THROWS_WITH(
        static_cast<void>(ogplay::loader::ParseAndroidBinaryManifest(invalid_offset)),
        "binary XML string offset is outside string data");

    auto invalid_size = Manifest();
    Set32(invalid_size, 4, static_cast<std::uint32_t>(invalid_size.size() - 1U));
    CHECK_THROWS_WITH(
        static_cast<void>(ogplay::loader::ParseAndroidBinaryManifest(invalid_size)),
        "AndroidManifest.xml is not one complete binary XML document");
}
