#include <doctest/doctest.h>

#include <algorithm>
#include <array>
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
    std::vector<std::byte> bytes(0x300, std::byte{});
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
    Put32(bytes, 68, 0x300);
    Put32(bytes, 72, 0x380);
    Put32(bytes, 76, 5);
    Put32(bytes, 80, 0x1000);

    Put32(bytes, 84, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 88, 0x100);
    Put32(bytes, 92, 0x10100);
    Put32(bytes, 100, 64);
    Put32(bytes, 104, 64);
    Put32(bytes, 108, 6);
    Put32(bytes, 112, 4);

    Put32(bytes, 0x100, ogplay::loader::kElfDynamicNeeded);
    Put32(bytes, 0x104, 1);
    Put32(bytes, 0x108, ogplay::loader::kElfDynamicStringTable);
    Put32(bytes, 0x10c, 0x10160);
    Put32(bytes, 0x110, ogplay::loader::kElfDynamicStringTableSize);
    Put32(bytes, 0x114, 30);
    Put32(bytes, 0x118, ogplay::loader::kElfDynamicSoname);
    Put32(bytes, 0x11c, 9);
    Put32(bytes, 0x120, ogplay::loader::kElfDynamicHash);
    Put32(bytes, 0x124, 0x10190);
    Put32(bytes, 0x128, ogplay::loader::kElfDynamicSymbolTable);
    Put32(bytes, 0x12c, 0x101b0);
    Put32(bytes, 0x130, ogplay::loader::kElfDynamicSymbolEntrySize);
    Put32(bytes, 0x134, 16);
    Put32(bytes, 0x138, 0);
    Put32(bytes, 0x13c, 0);
    const char strings[] = "\0libc.so\0sample.so\0foo\0hidden";
    for (std::size_t index = 0; index < sizeof(strings); ++index) {
        bytes[0x160 + index] = static_cast<std::byte>(strings[index]);
    }
    Put32(bytes, 0x190, 1);
    Put32(bytes, 0x194, 3);
    Put32(bytes, 0x198, 1);
    Put32(bytes, 0x19c, 0);
    Put32(bytes, 0x1a0, 2);
    Put32(bytes, 0x1a4, 0);

    Put32(bytes, 0x1c0, 19);
    Put32(bytes, 0x1c4, 0x10280);
    Put32(bytes, 0x1c8, 4);
    bytes[0x1cc] = std::byte{0x12};
    Put16(bytes, 0x1ce, 1);
    Put32(bytes, 0x1d0, 23);
    Put32(bytes, 0x1d4, 0x10284);
    Put32(bytes, 0x1d8, 4);
    bytes[0x1dc] = std::byte{0x11};
    bytes[0x1dd] = std::byte{2};
    Put16(bytes, 0x1de, 1);
    return bytes;
}

[[nodiscard]] std::vector<std::byte> GnuHashElf() {
    auto bytes = ValidElf();
    Put32(bytes, 0x120, ogplay::loader::kElfDynamicGnuHash);
    Put32(bytes, 0x190, 1);
    Put32(bytes, 0x194, 1);
    Put32(bytes, 0x198, 1);
    Put32(bytes, 0x19c, 5);
    Put32(bytes, 0x1a0, 0);
    Put32(bytes, 0x1a4, 1);
    Put32(bytes, 0x1a8, 0x100);
    Put32(bytes, 0x1ac, 0x201);
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
    CHECK(image.program_headers[0].file_size == 0x300);
    CHECK(image.program_headers[0].memory_size == 0x380);
    CHECK(image.has_dynamic_segment);
    REQUIRE(image.dynamic_entries.size() == 7);
    CHECK(image.dynamic_entries[0].tag == 1);
    CHECK(image.dynamic_entries[0].value == 1);
    CHECK(image.dynamic_entries[1].tag == 5);
    CHECK(image.dynamic_entries[1].value == 0x10160);
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
        Put32(bytes, 0x138, 6);
        CHECK_THROWS_AS(ParseAndDiscard(bytes), ogplay::loader::ElfError);
    }
    SUBCASE("multiple dynamic segments") {
        auto bytes = ValidElf();
        Put32(bytes, 52, ogplay::loader::kElfProgramDynamic);
        CHECK_THROWS_AS(ParseAndDiscard(bytes), ogplay::loader::ElfError);
    }
    SUBCASE("dynamic size is not entry aligned") {
        auto bytes = ValidElf();
        Put32(bytes, 100, 63);
        CHECK_THROWS_AS(ParseAndDiscard(bytes), ogplay::loader::ElfError);
    }
}

TEST_CASE("ELF32 dynamic info resolves needed and soname within file-backed load") {
    const auto bytes = ValidElf();
    const auto image = ogplay::loader::ParseElf32Arm(bytes);
    const auto dynamic = ogplay::loader::ReadElf32DynamicInfo(bytes, image);
    CHECK(dynamic.string_table == ogplay::memory::GuestAddress{0x10160});
    CHECK(dynamic.string_table_size == 30);
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
        Put32(bytes, 0x10c, 0x102f0);
        Put32(bytes, 0x114, 32);
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::loader::ReadElf32DynamicInfo(bytes, image)),
            ogplay::loader::ElfError);
    }
    SUBCASE("soname is not terminated inside the table") {
        auto bytes = ValidElf();
        for (std::size_t offset = 0x169; offset <= 0x17d; ++offset) {
            if (bytes[offset] == std::byte{}) bytes[offset] = std::byte{'x'};
        }
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::loader::ReadElf32DynamicInfo(bytes, image)),
            ogplay::loader::ElfError);
    }
}

TEST_CASE("ELF32 symbols use SysV hash count and preserve visibility") {
    const auto bytes = ValidElf();
    const auto image = ogplay::loader::ParseElf32Arm(bytes);
    const auto table = ogplay::loader::ReadElf32SymbolTable(bytes, image);
    REQUIRE(table.sysv_hash.has_value());
    CHECK(table.sysv_hash->bucket_count == 1);
    CHECK(table.sysv_hash->chain_count == 3);
    CHECK_FALSE(table.gnu_hash.has_value());
    REQUIRE(table.symbols.size() == 3);
    CHECK(table.symbols[0].name.empty());
    CHECK(table.symbols[1].name == "foo");
    CHECK(table.symbols[1].value == ogplay::memory::GuestAddress{0x10280});
    CHECK(table.symbols[1].binding == 1);
    CHECK(table.symbols[1].type == 2);
    CHECK(table.symbols[1].IsExported());
    CHECK(table.symbols[2].name == "hidden");
    CHECK(table.symbols[2].visibility == 2);
    CHECK_FALSE(table.symbols[2].IsExported());
}

TEST_CASE("ELF32 symbols derive count from a bounded GNU hash chain") {
    const auto bytes = GnuHashElf();
    const auto image = ogplay::loader::ParseElf32Arm(bytes);
    const auto table = ogplay::loader::ReadElf32SymbolTable(bytes, image);
    CHECK_FALSE(table.sysv_hash.has_value());
    REQUIRE(table.gnu_hash.has_value());
    CHECK(table.gnu_hash->bucket_count == 1);
    CHECK(table.gnu_hash->symbol_offset == 1);
    CHECK(table.gnu_hash->bloom_size == 1);
    CHECK(table.gnu_hash->symbol_count == 3);
    REQUIRE(table.symbols.size() == 3);
    CHECK(table.symbols[1].name == "foo");
}

TEST_CASE("ELF32 symbol metadata rejects malformed hash and table bounds") {
    SUBCASE("wrong symbol entry size") {
        auto bytes = ValidElf();
        Put32(bytes, 0x134, 12);
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32SymbolTable(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("missing supported hash") {
        auto bytes = ValidElf();
        Put32(bytes, 0x120, 0x6000000d);
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32SymbolTable(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("SysV hash size overflows") {
        auto bytes = ValidElf();
        Put32(bytes, 0x194, 0xffffffff);
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32SymbolTable(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("symbol table crosses file-backed load") {
        auto bytes = ValidElf();
        Put32(bytes, 0x12c, 0x102f0);
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32SymbolTable(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("symbol name is outside the string table") {
        auto bytes = ValidElf();
        Put32(bytes, 0x1c0, 30);
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32SymbolTable(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("symbol zero is not null") {
        auto bytes = ValidElf();
        Put32(bytes, 0x1b4, 1);
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32SymbolTable(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("GNU hash chain is unterminated") {
        auto bytes = GnuHashElf();
        for (std::size_t offset = 0x1a8; offset + 4 <= bytes.size();
             offset += 4) {
            Put32(bytes, offset, 0);
        }
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32SymbolTable(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("GNU bucket precedes symbol offset") {
        auto bytes = GnuHashElf();
        Put32(bytes, 0x194, 2);
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32SymbolTable(bytes, image)),
                        ogplay::loader::ElfError);
    }
}

TEST_CASE("ELF32 load maps bytes zeros BSS and applies final W xor X") {
    const auto bytes = ValidElf();
    const auto image = ogplay::loader::ParseElf32Arm(bytes);
    ogplay::memory::AddressSpace address_space;
    const auto plan = ogplay::loader::LoadElf32Arm(
        bytes, image, ogplay::memory::GuestAddress{0x20000}, address_space);

    REQUIRE(plan.regions.size() == 1);
    CHECK(plan.regions[0].range.Start() ==
          ogplay::memory::GuestAddress{0x30000});
    CHECK(plan.regions[0].final_protection ==
          (ogplay::memory::PageProtection::read |
           ogplay::memory::PageProtection::execute));
    REQUIRE(plan.entry.has_value());
    CHECK(*plan.entry == ogplay::memory::GuestAddress{0x30100});

    std::vector<std::byte> loaded(bytes.size());
    address_space.Read(ogplay::memory::GuestAddress{0x30000}, loaded);
    CHECK(loaded == bytes);
    std::vector<std::byte> bss(0x80, std::byte{0xff});
    address_space.Read(ogplay::memory::GuestAddress{0x30300}, bss);
    CHECK(std::all_of(bss.begin(), bss.end(),
                      [](const auto value) { return value == std::byte{}; }));
    CHECK_THROWS_AS(address_space.Write(ogplay::memory::GuestAddress{0x30000},
                                       std::vector<std::byte>{std::byte{1}}),
                    ogplay::memory::MemoryFault);
    std::array<std::byte, 4> instruction{};
    CHECK_NOTHROW(address_space.Fetch(ogplay::memory::GuestAddress{0x30100},
                                     instruction));
}

TEST_CASE("ELF32 load plan rejects unsafe bias entry and permissions") {
    SUBCASE("unaligned bias") {
        const auto image = ogplay::loader::ParseElf32Arm(ValidElf());
        CHECK_THROWS_AS(static_cast<void>(ogplay::loader::BuildElf32LoadPlan(
                            image, ogplay::memory::GuestAddress{0x20001}, 0x1000)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("biased segment wraps") {
        const auto image = ogplay::loader::ParseElf32Arm(ValidElf());
        CHECK_THROWS_AS(static_cast<void>(ogplay::loader::BuildElf32LoadPlan(
                            image, ogplay::memory::GuestAddress{0xffff0000}, 0x1000)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("page requires write and execute") {
        auto bytes = ValidElf();
        Put32(bytes, 76, 7);
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(static_cast<void>(ogplay::loader::BuildElf32LoadPlan(
                            image, ogplay::memory::GuestAddress{0x20000}, 0x1000)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("entry is outside executable load") {
        auto bytes = ValidElf();
        Put32(bytes, 24, 0x20000);
        const auto image = ogplay::loader::ParseElf32Arm(bytes);
        CHECK_THROWS_AS(static_cast<void>(ogplay::loader::BuildElf32LoadPlan(
                            image, ogplay::memory::GuestAddress{0x20000}, 0x1000)),
                        ogplay::loader::ElfError);
    }
}

TEST_CASE("ELF32 load leaves an existing address space unchanged on collision") {
    const auto bytes = ValidElf();
    const auto image = ogplay::loader::ParseElf32Arm(bytes);
    ogplay::memory::AddressSpace address_space;
    const auto page_size = address_space.PageSize();
    const ogplay::memory::GuestAddress occupied{0x30000};
    address_space.Map({occupied, page_size},
                      ogplay::memory::PageProtection::read |
                          ogplay::memory::PageProtection::write);
    const std::array marker{std::byte{0x5a}};
    address_space.Write(occupied, marker);
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::LoadElf32Arm(
                        bytes, image, ogplay::memory::GuestAddress{0x20000},
                        address_space)),
                    std::logic_error);
    std::array<std::byte, 1> actual{};
    address_space.Read(occupied, actual);
    CHECK(actual == marker);
}
