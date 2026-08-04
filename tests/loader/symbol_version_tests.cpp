#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ogplay/loader/symbol_version.h"

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

struct VersionFixture final {
    std::vector<std::byte> bytes{0x200, std::byte{}};
    ogplay::loader::Elf32Image image;
    ogplay::loader::Elf32DynamicInfo dynamic;
    ogplay::loader::Elf32SymbolTable symbols;

    VersionFixture() {
        image.program_headers.push_back(
            {ogplay::loader::kElfProgramLoad, 0,
             ogplay::memory::GuestAddress{0x1000}, 0x200, 0x200, 4, 0x1000});
        image.dynamic_entries = {
            {ogplay::loader::kElfDynamicVersionSymbol, 0x1040},
            {ogplay::loader::kElfDynamicVersionDefinition, 0x1060},
            {ogplay::loader::kElfDynamicVersionDefinitionCount, 1},
            {ogplay::loader::kElfDynamicVersionNeeded, 0x1080},
            {ogplay::loader::kElfDynamicVersionNeededCount, 1},
        };
        dynamic.string_table = ogplay::memory::GuestAddress{0x1100};
        dynamic.string_table_size = 27;
        const char strings[] = "\0LIB_1.0\0libdep.so\0DEP_2.0";
        for (std::size_t index = 0; index < sizeof(strings); ++index) {
            bytes[0x100 + index] = static_cast<std::byte>(strings[index]);
        }
        symbols.symbols.resize(4, {"", ogplay::memory::GuestAddress{},
                                   0, 0, 0, 0, 0});
        Put16(bytes, 0x40, 0);
        Put16(bytes, 0x42, 1);
        Put16(bytes, 0x44, 2);
        Put16(bytes, 0x46, 0x8003);

        Put16(bytes, 0x60, 1);
        Put16(bytes, 0x64, 2);
        Put16(bytes, 0x66, 1);
        Put32(bytes, 0x6c, 20);
        Put32(bytes, 0x70, 0);
        Put32(bytes, 0x74, 1);
        Put32(bytes, 0x78, 0);

        Put16(bytes, 0x80, 1);
        Put16(bytes, 0x82, 1);
        Put32(bytes, 0x84, 9);
        Put32(bytes, 0x88, 16);
        Put32(bytes, 0x8c, 0);
        Put16(bytes, 0x96, 3);
        Put32(bytes, 0x98, 19);
        Put32(bytes, 0x9c, 0);
    }
};

}  // namespace

TEST_CASE("ELF32 symbol versions map definitions and dependency requirements") {
    const VersionFixture fixture;
    const auto versions = ogplay::loader::ReadElf32SymbolVersions(
        fixture.bytes, fixture.image, fixture.dynamic, fixture.symbols);
    REQUIRE(versions.has_value());
    REQUIRE(versions->symbols.size() == 4);
    CHECK(versions->symbols[0].kind ==
          ogplay::loader::Elf32SymbolVersionKind::local);
    CHECK(versions->symbols[1].kind ==
          ogplay::loader::Elf32SymbolVersionKind::global);
    CHECK(versions->symbols[2].kind ==
          ogplay::loader::Elf32SymbolVersionKind::definition);
    CHECK(versions->symbols[2].name == "LIB_1.0");
    CHECK(versions->symbols[3].kind ==
          ogplay::loader::Elf32SymbolVersionKind::requirement);
    CHECK(versions->symbols[3].name == "DEP_2.0");
    CHECK(versions->symbols[3].dependency == "libdep.so");
    CHECK(versions->symbols[3].hidden);
}

TEST_CASE("ELF32 base version definition keeps index one global") {
    auto fixture = VersionFixture();
    Put16(fixture.bytes, 0x62, 1);
    Put16(fixture.bytes, 0x64, 1);
    Put16(fixture.bytes, 0x44, 1);
    const auto versions = ogplay::loader::ReadElf32SymbolVersions(
        fixture.bytes, fixture.image, fixture.dynamic, fixture.symbols);
    REQUIRE(versions.has_value());
    CHECK(versions->symbols[2].kind ==
          ogplay::loader::Elf32SymbolVersionKind::global);
}

TEST_CASE("ELF32 symbol versions reject incomplete and inconsistent metadata") {
    SUBCASE("no version metadata is an explicit empty fact") {
        auto fixture = VersionFixture();
        fixture.image.dynamic_entries.clear();
        CHECK_FALSE(ogplay::loader::ReadElf32SymbolVersions(
                        fixture.bytes, fixture.image, fixture.dynamic,
                        fixture.symbols)
                        .has_value());
    }
    SUBCASE("definition count is missing") {
        auto fixture = VersionFixture();
        fixture.image.dynamic_entries.erase(
            fixture.image.dynamic_entries.begin() + 2);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32SymbolVersions(
                                fixture.bytes, fixture.image, fixture.dynamic,
                                fixture.symbols)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("versym references an unknown index") {
        auto fixture = VersionFixture();
        Put16(fixture.bytes, 0x44, 4);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32SymbolVersions(
                                fixture.bytes, fixture.image, fixture.dynamic,
                                fixture.symbols)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("verneed chain disagrees with its count") {
        auto fixture = VersionFixture();
        Put32(fixture.bytes, 0x8c, 16);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32SymbolVersions(
                                fixture.bytes, fixture.image, fixture.dynamic,
                                fixture.symbols)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("verdef auxiliary chain disagrees with its count") {
        auto fixture = VersionFixture();
        Put16(fixture.bytes, 0x66, 2);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32SymbolVersions(
                                fixture.bytes, fixture.image, fixture.dynamic,
                                fixture.symbols)),
                        ogplay::loader::ElfError);
    }
}
