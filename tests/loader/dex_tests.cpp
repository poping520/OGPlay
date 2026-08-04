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
