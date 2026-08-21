#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
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

std::vector<std::uint8_t> ClassDex(const char* second_type = "I") {
    constexpr std::uint32_t string_ids = 0x70;
    constexpr std::uint32_t type_ids = 0x8c;
    constexpr std::uint32_t proto_ids = 0x98;
    constexpr std::uint32_t field_ids = 0xa4;
    constexpr std::uint32_t method_ids = 0xac;
    constexpr std::uint32_t class_defs = 0xb4;
    constexpr std::uint32_t data_offset = 0xd4;
    constexpr std::uint32_t map_offset = 0x108;
    constexpr std::uint32_t file_size = 0x178;
    auto bytes = MinimalDex();
    bytes.resize(file_size);
    Put32(bytes, 32, file_size);
    Put32(bytes, 52, map_offset);
    Put32(bytes, 56, 7);
    Put32(bytes, 60, string_ids);
    Put32(bytes, 64, 3);
    Put32(bytes, 68, type_ids);
    Put32(bytes, 72, 1);
    Put32(bytes, 76, proto_ids);
    Put32(bytes, 80, 1);
    Put32(bytes, 84, field_ids);
    Put32(bytes, 88, 1);
    Put32(bytes, 92, method_ids);
    Put32(bytes, 96, 1);
    Put32(bytes, 100, class_defs);
    Put32(bytes, 104, file_size - data_offset);
    Put32(bytes, 108, data_offset);

    std::size_t cursor = data_offset;
    const char* values[]{"V", second_type, "Lsample/Peer;", "VI", "run",
                         "value", "Source.java"};
    for (std::size_t index = 0; index < 7; ++index) {
        Put32(bytes, string_ids + index * 4, static_cast<std::uint32_t>(cursor));
        cursor = PutString(bytes, cursor, values[index]);
    }
    Put32(bytes, type_ids, 0);
    Put32(bytes, type_ids + 4, 1);
    Put32(bytes, type_ids + 8, 2);
    Put32(bytes, proto_ids, 0);
    Put32(bytes, proto_ids + 4, 0);
    Put32(bytes, proto_ids + 8, 0);
    Put16(bytes, field_ids, 2);
    Put16(bytes, field_ids + 2, 1);
    Put32(bytes, field_ids + 4, 5);
    Put16(bytes, method_ids, 2);
    Put16(bytes, method_ids + 2, 0);
    Put32(bytes, method_ids + 4, 4);
    Put32(bytes, class_defs, 2);
    Put32(bytes, class_defs + 4, 1);
    Put32(bytes, class_defs + 8, 0xffffffff);
    Put32(bytes, class_defs + 16, 6);

    Put32(bytes, map_offset, 9);
    PutMap(bytes, map_offset + 4, 0x0000, 1, 0);
    PutMap(bytes, map_offset + 16, 0x0001, 7, string_ids);
    PutMap(bytes, map_offset + 28, 0x0002, 3, type_ids);
    PutMap(bytes, map_offset + 40, 0x0003, 1, proto_ids);
    PutMap(bytes, map_offset + 52, 0x0004, 1, field_ids);
    PutMap(bytes, map_offset + 64, 0x0005, 1, method_ids);
    PutMap(bytes, map_offset + 76, 0x0006, 1, class_defs);
    PutMap(bytes, map_offset + 88, 0x2002, 7, data_offset);
    PutMap(bytes, map_offset + 100, 0x1000, 1, map_offset);
    return bytes;
}

std::vector<std::uint8_t> ReadDexFixture(const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing DEX fixture: ", path);
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

std::uint32_t Read32(const std::vector<std::uint8_t>& bytes,
                     const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void SkipUleb128(const std::vector<std::uint8_t>& bytes,
                 std::size_t& offset) {
    while ((bytes[offset++] & 0x80U) != 0U) {
    }
}

std::size_t FirstSystemAnnotationValue(
    const std::vector<std::uint8_t>& bytes) {
    const auto image = ogplay::loader::ParseDex(bytes);
    const auto annotated = std::find_if(
        image.classes.begin(), image.classes.end(),
        [](const auto& item) { return item.annotations_offset != 0U; });
    REQUIRE(annotated != image.classes.end());
    const auto annotation_set = Read32(bytes, annotated->annotations_offset);
    REQUIRE(annotation_set != 0U);
    REQUIRE(Read32(bytes, annotation_set) != 0U);
    const auto annotation_item = Read32(bytes, annotation_set + 4U);
    std::size_t offset = annotation_item + 1U;  // visibility
    SkipUleb128(bytes, offset);                // annotation type
    SkipUleb128(bytes, offset);                // element count
    SkipUleb128(bytes, offset);                // first element name
    return offset;
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

TEST_CASE("DEX member IDs and class definitions resolve fixed facts") {
    const auto image = ogplay::loader::ParseDex(ClassDex());
    REQUIRE(image.fields.size() == 1);
    CHECK(image.fields[0].class_type_index == 2);
    CHECK(image.fields[0].type_index == 1);
    REQUIRE(image.methods.size() == 1);
    CHECK(image.methods[0].prototype_index == 0);
    REQUIRE(image.classes.size() == 1);
    CHECK(image.classes[0].class_type_index == 2);
    CHECK(image.classes[0].source_file_string_index == 6);
    CHECK_FALSE(image.classes[0].superclass_type_index.has_value());
}

TEST_CASE("DEX member IDs reject invalid declaring and referenced indices") {
    auto bad_field_class = ClassDex();
    Put16(bad_field_class, 0xa4, 1);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::ParseDex(bad_field_class)),
        ogplay::loader::DexError);
    auto bad_method_name = ClassDex();
    Put32(bad_method_name, 0xac + 4, 99);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::ParseDex(bad_method_name)),
        ogplay::loader::DexError);
}

// dex-format 允许数组 descriptor 作为 method 引用的 owner(数组继承
// Object.clone();AOSP libdex/DexFile.h 与 docs/dex-format 均不限制
// method_id 的 class_idx 指向 class 类型),field 引用的 owner 不允许。
TEST_CASE("DEX method_id accepts an array type as declaring owner") {
    auto array_owner = ClassDex("[I");
    Put16(array_owner, 0xac, 1);
    const auto image = ogplay::loader::ParseDex(array_owner);
    REQUIRE(image.methods.size() == 1);
    CHECK(image.methods[0].class_type_index == 1);
    CHECK(image.types[1].descriptor == "[I");
}

TEST_CASE("DEX method_id still rejects a primitive declaring owner") {
    auto primitive_owner = ClassDex();
    Put16(primitive_owner, 0xac, 1);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::ParseDex(primitive_owner)),
        ogplay::loader::DexError);
}

TEST_CASE("DEX field_id still rejects an array type as declaring owner") {
    auto array_field_owner = ClassDex("[I");
    Put16(array_field_owner, 0xa4, 1);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::ParseDex(array_field_owner)),
        ogplay::loader::DexError);
}

TEST_CASE("DEX class definitions reject invalid types sources and offsets") {
    auto bad_class = ClassDex();
    Put32(bad_class, 0xb4, 1);
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(bad_class)),
                    ogplay::loader::DexError);
    auto bad_source = ClassDex();
    Put32(bad_source, 0xb4 + 16, 99);
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(bad_source)),
                    ogplay::loader::DexError);
    auto bad_data = ClassDex();
    Put32(bad_data, 0xb4 + 24, 0x80);
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(bad_data)),
                    ogplay::loader::DexError);
}

TEST_CASE("DEX system annotations reject invalid encoded values and value_arg") {
    auto invalid_type = ReadDexFixture("reflection.dex");
    const auto value_offset = FirstSystemAnnotationValue(invalid_type);
    invalid_type[value_offset] = 0x01U;  // reserved encoded_value type
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::ParseDex(invalid_type)),
        ogplay::loader::DexError);

    auto invalid_argument = ReadDexFixture("reflection.dex");
    invalid_argument[FirstSystemAnnotationValue(invalid_argument)] =
        0x3cU;  // VALUE_ARRAY with forbidden value_arg=1
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::ParseDex(invalid_argument)),
        ogplay::loader::DexError);
}
