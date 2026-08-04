#include <cstddef>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/loader/dex_class_data.h"

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

std::vector<std::uint8_t> ClassDataDex() {
    constexpr std::uint32_t string_ids = 0x70;
    constexpr std::uint32_t type_ids = 0x7c;
    constexpr std::uint32_t proto_ids = 0x84;
    constexpr std::uint32_t method_ids = 0x90;
    constexpr std::uint32_t class_defs = 0x98;
    constexpr std::uint32_t data_offset = 0xb8;
    constexpr std::uint32_t class_data = 0xd0;
    constexpr std::uint32_t code = 0xd8;
    constexpr std::uint32_t map_offset = 0xf0;
    constexpr std::uint32_t file_size = 0x16c;
    std::vector<std::uint8_t> bytes(file_size);
    const std::uint8_t magic[]{'d', 'e', 'x', '\n', '0', '3', '5', 0};
    for (std::size_t index = 0; index < sizeof(magic); ++index) {
        bytes[index] = magic[index];
    }
    Put32(bytes, 32, file_size);
    Put32(bytes, 36, 0x70);
    Put32(bytes, 40, 0x12345678);
    Put32(bytes, 52, map_offset);
    Put32(bytes, 56, 3);
    Put32(bytes, 60, string_ids);
    Put32(bytes, 64, 2);
    Put32(bytes, 68, type_ids);
    Put32(bytes, 72, 1);
    Put32(bytes, 76, proto_ids);
    Put32(bytes, 88, 1);
    Put32(bytes, 92, method_ids);
    Put32(bytes, 96, 1);
    Put32(bytes, 100, class_defs);
    Put32(bytes, 104, file_size - data_offset);
    Put32(bytes, 108, data_offset);
    std::size_t cursor = data_offset;
    const char* strings[]{"V", "Lsample/Peer;", "run"};
    for (std::size_t index = 0; index < 3; ++index) {
        Put32(bytes, string_ids + index * 4, static_cast<std::uint32_t>(cursor));
        cursor = PutString(bytes, cursor, strings[index]);
    }
    Put32(bytes, type_ids, 0);
    Put32(bytes, type_ids + 4, 1);
    Put32(bytes, proto_ids, 0);
    Put32(bytes, proto_ids + 4, 0);
    Put16(bytes, method_ids, 1);
    Put32(bytes, method_ids + 4, 2);
    Put32(bytes, class_defs, 1);
    Put32(bytes, class_defs + 4, 1);
    Put32(bytes, class_defs + 8, 0xffffffff);
    Put32(bytes, class_defs + 16, 0xffffffff);
    Put32(bytes, class_defs + 24, class_data);
    bytes[class_data] = 0;
    bytes[class_data + 1] = 0;
    bytes[class_data + 2] = 1;
    bytes[class_data + 3] = 0;
    bytes[class_data + 4] = 0;
    bytes[class_data + 5] = 9;
    bytes[class_data + 6] = 0xd8;
    bytes[class_data + 7] = 1;
    Put16(bytes, code, 1);
    Put16(bytes, code + 2, 0);
    Put32(bytes, code + 12, 4);
    Put32(bytes, map_offset, 10);
    PutMap(bytes, map_offset + 4, 0x0000, 1, 0);
    PutMap(bytes, map_offset + 16, 0x0001, 3, string_ids);
    PutMap(bytes, map_offset + 28, 0x0002, 2, type_ids);
    PutMap(bytes, map_offset + 40, 0x0003, 1, proto_ids);
    PutMap(bytes, map_offset + 52, 0x0005, 1, method_ids);
    PutMap(bytes, map_offset + 64, 0x0006, 1, class_defs);
    PutMap(bytes, map_offset + 76, 0x2002, 3, data_offset);
    PutMap(bytes, map_offset + 88, 0x2000, 1, class_data);
    PutMap(bytes, map_offset + 100, 0x2001, 1, code);
    PutMap(bytes, map_offset + 112, 0x1000, 1, map_offset);
    return bytes;
}

}  // namespace

TEST_CASE("DEX class_data resolves delta members and code metadata") {
    const auto bytes = ClassDataDex();
    const auto image = ogplay::loader::ParseDex(bytes);
    const auto classes = ogplay::loader::ReadDexClassData(bytes, image);
    REQUIRE(classes.size() == 1);
    REQUIRE(classes[0].direct_methods.size() == 1);
    CHECK(classes[0].direct_methods[0].method_index == 0);
    REQUIRE(classes[0].direct_methods[0].code.has_value());
    CHECK(classes[0].direct_methods[0].code->registers_size == 1);
    CHECK(classes[0].direct_methods[0].code->instruction_units == 4);
}

TEST_CASE("DEX class_data rejects missing code and invalid member deltas") {
    auto missing_code = ClassDataDex();
    missing_code[0xd0 + 6] = 0;
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::ReadDexClassData(
            missing_code, ogplay::loader::ParseDex(missing_code))),
        ogplay::loader::DexError);
    auto bad_member = ClassDataDex();
    bad_member[0xd0 + 4] = 2;
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::ReadDexClassData(
            bad_member, ogplay::loader::ParseDex(bad_member))),
        ogplay::loader::DexError);
}

TEST_CASE("DEX code metadata rejects alignment ranges and register errors") {
    auto unaligned = ClassDataDex();
    unaligned[0xd0 + 6] = 0xd9;
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::ReadDexClassData(
            unaligned, ogplay::loader::ParseDex(unaligned))),
        ogplay::loader::DexError);
    auto huge_code = ClassDataDex();
    Put32(huge_code, 0xd8 + 12, 0xffffffff);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::ReadDexClassData(
            huge_code, ogplay::loader::ParseDex(huge_code))),
        ogplay::loader::DexError);
    auto bad_registers = ClassDataDex();
    Put16(bad_registers, 0xd8, 0);
    Put16(bad_registers, 0xd8 + 2, 1);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::ReadDexClassData(
            bad_registers, ogplay::loader::ParseDex(bad_registers))),
        ogplay::loader::DexError);
}
