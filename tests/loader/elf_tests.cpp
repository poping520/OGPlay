#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ogplay/loader/elf.h"

namespace {

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

[[nodiscard]] std::vector<std::byte> ValidElf() {
    std::vector<std::byte> bytes(0x180, std::byte{});
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
    Put32(bytes, 24, 0x10100);
    Put32(bytes, 28, 52);
    Put32(bytes, 36, 0x05000400);
    Put16(bytes, 40, 52);
    Put16(bytes, 42, 32);
    Put16(bytes, 44, 2);

    Put32(bytes, 52, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 56, 0);
    Put32(bytes, 60, 0x10000);
    Put32(bytes, 68, 0x180);
    Put32(bytes, 72, 0x200);
    Put32(bytes, 76, 5);
    Put32(bytes, 80, 0x1000);

    Put32(bytes, 84, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 88, 0x100);
    Put32(bytes, 92, 0x10100);
    Put32(bytes, 100, 40);
    Put32(bytes, 104, 40);
    Put32(bytes, 108, 6);
    Put32(bytes, 112, 4);

    Put32(bytes, 0x100, ogplay::loader::kElfDynamicNeeded);
    Put32(bytes, 0x104, 1);
    Put32(bytes, 0x108, ogplay::loader::kElfDynamicStringTable);
    Put32(bytes, 0x10c, 0x10140);
    Put32(bytes, 0x110, ogplay::loader::kElfDynamicStringTableSize);
    Put32(bytes, 0x114, 19);
    Put32(bytes, 0x118, ogplay::loader::kElfDynamicSoname);
    Put32(bytes, 0x11c, 9);
    Put32(bytes, 0x120, 0);
    Put32(bytes, 0x124, 0);
    const char strings[] = "\0libc.so\0sample.so";
    for (std::size_t index = 0; index < sizeof(strings); ++index) {
        bytes[0x140 + index] = static_cast<std::byte>(strings[index]);
    }
    return bytes;
}

void ParseAndDiscard(const std::vector<std::byte>& bytes) {
    static_cast<void>(ogplay::loader::ParseElf32Arm(bytes));
}

}  // namespace

TEST_CASE("ELF32 ARM parser preserves load and dynamic facts") {
    const auto image = ogplay::loader::ParseElf32Arm(ValidElf());
    CHECK(image.type == ogplay::loader::Elf32ImageType::shared_object);
    CHECK(image.entry == ogplay::memory::GuestAddress{0x10100});
    CHECK(image.arm_flags == 0x05000400);
    REQUIRE(image.program_headers.size() == 2);
    CHECK(image.program_headers[0].type == ogplay::loader::kElfProgramLoad);
    CHECK(image.program_headers[0].file_size == 0x180);
    CHECK(image.program_headers[0].memory_size == 0x200);
    CHECK(image.has_dynamic_segment);
    REQUIRE(image.dynamic_entries.size() == 4);
    CHECK(image.dynamic_entries[0].tag == 1);
    CHECK(image.dynamic_entries[0].value == 1);
    CHECK(image.dynamic_entries[1].tag == 5);
    CHECK(image.dynamic_entries[1].value == 0x10140);
}

TEST_CASE("ELF32 ARM parser rejects malformed identity and tables") {
    SUBCASE("truncated header") {
        CHECK_THROWS_AS(ParseAndDiscard(std::vector<std::byte>(20)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("wrong class") {
        auto bytes = ValidElf();
        bytes[4] = std::byte{2};
        CHECK_THROWS_AS(ParseAndDiscard(bytes), ogplay::loader::ElfError);
    }
    SUBCASE("wrong machine") {
        auto bytes = ValidElf();
        Put16(bytes, 18, 183);
        CHECK_THROWS_AS(ParseAndDiscard(bytes), ogplay::loader::ElfError);
    }
    SUBCASE("program table outside image") {
        auto bytes = ValidElf();
        Put32(bytes, 28, 0xfffffff0);
        CHECK_THROWS_AS(ParseAndDiscard(bytes), ogplay::loader::ElfError);
    }
}

TEST_CASE("ELF32 ARM parser rejects unsafe load segments") {
    SUBCASE("file size exceeds memory size") {
        auto bytes = ValidElf();
        Put32(bytes, 72, 0x100);
        CHECK_THROWS_AS(ParseAndDiscard(bytes), ogplay::loader::ElfError);
    }
    SUBCASE("file range exceeds image") {
        auto bytes = ValidElf();
        Put32(bytes, 56, 0x100);
        CHECK_THROWS_AS(ParseAndDiscard(bytes), ogplay::loader::ElfError);
    }
    SUBCASE("guest range wraps") {
        auto bytes = ValidElf();
        Put32(bytes, 60, 0xffffff80);
        Put32(bytes, 80, 1);
        CHECK_THROWS_AS(ParseAndDiscard(bytes), ogplay::loader::ElfError);
    }
    SUBCASE("alignment is incongruent") {
        auto bytes = ValidElf();
        Put32(bytes, 56, 4);
        CHECK_THROWS_AS(ParseAndDiscard(bytes), ogplay::loader::ElfError);
    }
}

TEST_CASE("ELF32 ARM parser requires one terminated dynamic segment") {
    SUBCASE("missing terminator") {
        auto bytes = ValidElf();
        Put32(bytes, 0x120, 6);
        CHECK_THROWS_AS(ParseAndDiscard(bytes), ogplay::loader::ElfError);
    }
    SUBCASE("multiple dynamic segments") {
        auto bytes = ValidElf();
        Put32(bytes, 52, ogplay::loader::kElfProgramDynamic);
        CHECK_THROWS_AS(ParseAndDiscard(bytes), ogplay::loader::ElfError);
    }
    SUBCASE("dynamic size is not entry aligned") {
        auto bytes = ValidElf();
        Put32(bytes, 100, 39);
        CHECK_THROWS_AS(ParseAndDiscard(bytes), ogplay::loader::ElfError);
    }
}

TEST_CASE("ELF32 dynamic info resolves needed and soname within file-backed load") {
    const auto bytes = ValidElf();
    const auto image = ogplay::loader::ParseElf32Arm(bytes);
    const auto dynamic = ogplay::loader::ReadElf32DynamicInfo(bytes, image);
    CHECK(dynamic.string_table == ogplay::memory::GuestAddress{0x10140});
    CHECK(dynamic.string_table_size == 19);
    REQUIRE(dynamic.needed.size() == 1);
    CHECK(dynamic.needed[0] == "libc.so");
    REQUIRE(dynamic.soname.has_value());
    CHECK(*dynamic.soname == "sample.so");
}

TEST_CASE("ELF32 dynamic strings reject ambiguous or unbacked metadata") {
    SUBCASE("missing string table size") {
        auto bytes = ValidElf();
        Put32(bytes, 0x110, 6);
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::loader::ReadElf32DynamicInfo(bytes, image)),
            ogplay::loader::ElfError);
    }
    SUBCASE("duplicate string table") {
        auto bytes = ValidElf();
        Put32(bytes, 0x118, ogplay::loader::kElfDynamicStringTable);
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::loader::ReadElf32DynamicInfo(bytes, image)),
            ogplay::loader::ElfError);
    }
    SUBCASE("string table crosses file-backed load") {
        auto bytes = ValidElf();
        Put32(bytes, 0x10c, 0x10170);
        Put32(bytes, 0x114, 32);
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::loader::ReadElf32DynamicInfo(bytes, image)),
            ogplay::loader::ElfError);
    }
    SUBCASE("soname is not terminated inside the table") {
        auto bytes = ValidElf();
        bytes[0x152] = std::byte{'x'};
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::loader::ReadElf32DynamicInfo(bytes, image)),
            ogplay::loader::ElfError);
    }
}
