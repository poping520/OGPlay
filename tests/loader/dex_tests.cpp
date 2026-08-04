#include <cstddef>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/loader/dex.h"

namespace {

void Put16(std::vector<std::uint8_t>& bytes, const std::size_t offset,
           const std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

void Put32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
           const std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] =
            static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

void PutMap(std::vector<std::uint8_t>& bytes, const std::size_t offset,
            const std::uint16_t type, const std::uint32_t size,
            const std::uint32_t item_offset) {
    Put16(bytes, offset, type);
    Put16(bytes, offset + 2, 0);
    Put32(bytes, offset + 4, size);
    Put32(bytes, offset + 8, item_offset);
}

std::vector<std::uint8_t> MinimalDex() {
    constexpr std::uint32_t map_offset = 0x70;
    constexpr std::uint32_t file_size = 0x8c;
    std::vector<std::uint8_t> bytes(file_size);
    const std::uint8_t magic[]{'d', 'e', 'x', '\n', '0', '3', '5', 0};
    for (std::size_t index = 0; index < sizeof(magic); ++index) {
        bytes[index] = magic[index];
    }
    Put32(bytes, 32, file_size);
    Put32(bytes, 36, 0x70);
    Put32(bytes, 40, 0x12345678);
    Put32(bytes, 52, map_offset);
    Put32(bytes, 104, file_size - map_offset);
    Put32(bytes, 108, map_offset);
    Put32(bytes, map_offset, 2);
    PutMap(bytes, map_offset + 4, 0x0000, 1, 0);
    PutMap(bytes, map_offset + 16, 0x1000, 1, map_offset);
    return bytes;
}

std::size_t PutString(std::vector<std::uint8_t>& bytes, std::size_t offset,
                      const char* value) {
    std::size_t length{};
    while (value[length] != '\0') ++length;
    bytes[offset++] = static_cast<std::uint8_t>(length);
    for (std::size_t index = 0; index < length; ++index) {
        bytes[offset++] = static_cast<std::uint8_t>(value[index]);
    }
    bytes[offset++] = 0;
    return offset;
}

std::vector<std::uint8_t> TableDex() {
    constexpr std::uint32_t string_ids = 0x70;
    constexpr std::uint32_t type_ids = 0x84;
    constexpr std::uint32_t proto_ids = 0x90;
    constexpr std::uint32_t data_offset = 0x9c;
    constexpr std::uint32_t type_list = 0xbc;
    constexpr std::uint32_t map_offset = 0xc4;
    constexpr std::uint32_t file_size = 0x11c;
    auto bytes = MinimalDex();
    bytes.resize(file_size);
    Put32(bytes, 32, file_size);
    Put32(bytes, 52, map_offset);
    Put32(bytes, 56, 5);
    Put32(bytes, 60, string_ids);
    Put32(bytes, 64, 3);
    Put32(bytes, 68, type_ids);
    Put32(bytes, 72, 1);
    Put32(bytes, 76, proto_ids);
    Put32(bytes, 104, file_size - data_offset);
    Put32(bytes, 108, data_offset);

    std::size_t cursor = data_offset;
    const char* values[]{"V", "I", "Lsample/Peer;", "VI", "run"};
    for (std::size_t index = 0; index < 5; ++index) {
        Put32(bytes, string_ids + index * 4, static_cast<std::uint32_t>(cursor));
        cursor = PutString(bytes, cursor, values[index]);
    }
    Put32(bytes, type_ids, 0);
    Put32(bytes, type_ids + 4, 1);
    Put32(bytes, type_ids + 8, 2);
    Put32(bytes, proto_ids, 3);
    Put32(bytes, proto_ids + 4, 0);
    Put32(bytes, proto_ids + 8, type_list);
    Put32(bytes, type_list, 1);
    Put16(bytes, type_list + 4, 1);

    Put32(bytes, map_offset, 7);
    PutMap(bytes, map_offset + 4, 0x0000, 1, 0);
    PutMap(bytes, map_offset + 16, 0x0001, 5, string_ids);
    PutMap(bytes, map_offset + 28, 0x0002, 3, type_ids);
    PutMap(bytes, map_offset + 40, 0x0003, 1, proto_ids);
    PutMap(bytes, map_offset + 52, 0x2002, 5, data_offset);
    PutMap(bytes, map_offset + 64, 0x1001, 1, type_list);
    PutMap(bytes, map_offset + 76, 0x1000, 1, map_offset);
    return bytes;
}

}  // namespace

TEST_CASE("DEX parser preserves header and ordered map facts") {
    const auto image = ogplay::loader::ParseDex(MinimalDex());
    CHECK(image.header.version == "035");
    CHECK(image.header.file_size == 0x8c);
    REQUIRE(image.map_items.size() == 2);
    CHECK(image.map_items[0].type == 0x0000);
    CHECK(image.FindMapItem(ogplay::loader::DexMapItemType::map_list)->offset ==
          0x70);
}

TEST_CASE("DEX parser rejects identity endian and fixed table range errors") {
    auto bad_magic = MinimalDex();
    bad_magic[0] = 'x';
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(bad_magic)),
                    ogplay::loader::DexError);
    auto bad_endian = MinimalDex();
    Put32(bad_endian, 40, 0x78563412);
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(bad_endian)),
                    ogplay::loader::DexError);
    auto bad_table = MinimalDex();
    Put32(bad_table, 56, 2);
    Put32(bad_table, 60, 0x88);
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(bad_table)),
                    ogplay::loader::DexError);
}

TEST_CASE("DEX map rejects duplicate unordered and inconsistent entries") {
    auto duplicate = MinimalDex();
    Put16(duplicate, 0x70 + 16, 0x0000);
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(duplicate)),
                    ogplay::loader::DexError);
    auto unordered = MinimalDex();
    Put32(unordered, 0x70 + 16 + 8, 0x60);
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(unordered)),
                    ogplay::loader::DexError);
    auto mismatch = MinimalDex();
    Put32(mismatch, 52, 0x74);
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(mismatch)),
                    ogplay::loader::DexError);
}

TEST_CASE("DEX strings types and prototypes resolve indexed facts") {
    const auto image = ogplay::loader::ParseDex(TableDex());
    REQUIRE(image.strings.size() == 5);
    CHECK(image.strings[2].value == u"Lsample/Peer;");
    REQUIRE(image.types.size() == 3);
    CHECK(image.types[2].descriptor == "Lsample/Peer;");
    REQUIRE(image.prototypes.size() == 1);
    CHECK(image.prototypes[0].return_type_index == 0);
    CHECK(image.prototypes[0].parameter_type_indices ==
          std::vector<std::uint32_t>{1});
}

TEST_CASE("DEX strings reject malformed Modified UTF-8 and length") {
    auto malformed = TableDex();
    malformed[0x9d] = 0xf0;
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(malformed)),
                    ogplay::loader::DexError);
    auto bad_length = TableDex();
    bad_length[0x9c] = 2;
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(bad_length)),
                    ogplay::loader::DexError);
}

TEST_CASE("DEX type and prototype indices remain strictly checked") {
    auto bad_type = TableDex();
    Put32(bad_type, 0x84, 99);
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(bad_type)),
                    ogplay::loader::DexError);
    auto bad_parameter = TableDex();
    Put16(bad_parameter, 0xbc + 4, 9);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::ParseDex(bad_parameter)),
        ogplay::loader::DexError);
    auto bad_shorty = TableDex();
    bad_shorty[0xb2] = 'I';
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(bad_shorty)),
                    ogplay::loader::DexError);
}
