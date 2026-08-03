#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "ogplay/loader/relocation.h"

namespace {

using ogplay::memory::GuestAddress;
using ogplay::memory::GuestRange;
using ogplay::memory::PageProtection;

void WriteWord(ogplay::memory::AddressSpace& memory, const GuestAddress address,
               const std::uint32_t value) {
    std::array<std::byte, 4> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
    memory.Write(address, bytes);
}

[[nodiscard]] std::uint32_t ReadWord(
    const ogplay::memory::AddressSpace& memory, const GuestAddress address) {
    std::array<std::byte, 4> bytes{};
    memory.Read(address, bytes);
    std::uint32_t value{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[index]))
                 << static_cast<unsigned>(index * 8U);
    }
    return value;
}

struct Fixture final {
    ogplay::memory::AddressSpace memory;
    ogplay::loader::Elf32LoadPlan plan;

    Fixture() {
        const GuestRange page{GuestAddress{0x30000}, memory.PageSize()};
        memory.Map(page, PageProtection::read | PageProtection::write);
        plan.load_bias = GuestAddress{0x20000};
        plan.regions.push_back(
            {page, PageProtection::read | PageProtection::execute});
    }
};

}  // namespace

TEST_CASE("ARM REL applies common linker relocations and restores W xor X") {
    Fixture fixture;
    WriteWord(fixture.memory, GuestAddress{0x30280}, 4);
    WriteWord(fixture.memory, GuestAddress{0x30284}, 7);
    WriteWord(fixture.memory, GuestAddress{0x30288}, 8);
    WriteWord(fixture.memory, GuestAddress{0x30300}, 12);
    fixture.memory.Protect(fixture.plan.regions[0].range,
                           fixture.plan.regions[0].final_protection);

    ogplay::loader::Elf32RelocationTable table;
    table.relocations = {
        {GuestAddress{0x10280}, 1, ogplay::loader::kArmRelocationAbs32},
        {GuestAddress{0x10284}, 2, ogplay::loader::kArmRelocationJumpSlot},
        {GuestAddress{0x10288}, 1, ogplay::loader::kArmRelocationRel32},
        {GuestAddress{0x10300}, 0, ogplay::loader::kArmRelocationRelative},
    };
    ogplay::loader::Elf32ResolvedSymbols symbols;
    symbols.values = {std::nullopt, GuestAddress{0x50000},
                      GuestAddress{0x60000}};

    ogplay::loader::ApplyElf32ArmRelocations(
        table, symbols, GuestAddress{0x20000}, fixture.plan, fixture.memory);
    CHECK(ReadWord(fixture.memory, GuestAddress{0x30280}) == 0x50004);
    CHECK(ReadWord(fixture.memory, GuestAddress{0x30284}) == 0x60000);
    CHECK(ReadWord(fixture.memory, GuestAddress{0x30288}) == 0x1fd80);
    CHECK(ReadWord(fixture.memory, GuestAddress{0x30300}) == 0x2000c);
    const std::array byte{std::byte{1}};
    CHECK_THROWS_AS(fixture.memory.Write(GuestAddress{0x30280}, byte),
                    ogplay::memory::MemoryFault);
}

TEST_CASE("ARM REL rejects unresolved unsupported and ambiguous writes atomically") {
    Fixture fixture;
    WriteWord(fixture.memory, GuestAddress{0x30280}, 4);
    fixture.memory.Protect(fixture.plan.regions[0].range,
                           fixture.plan.regions[0].final_protection);
    ogplay::loader::Elf32ResolvedSymbols symbols;
    symbols.values = {std::nullopt, std::nullopt};

    SUBCASE("unresolved symbol") {
        ogplay::loader::Elf32RelocationTable table;
        table.relocations = {
            {GuestAddress{0x10280}, 1, ogplay::loader::kArmRelocationAbs32}};
        CHECK_THROWS_AS(ogplay::loader::ApplyElf32ArmRelocations(
                            table, symbols, GuestAddress{0x20000}, fixture.plan,
                            fixture.memory),
                        ogplay::loader::RelocationError);
    }
    SUBCASE("unknown relocation type") {
        ogplay::loader::Elf32RelocationTable table;
        table.relocations = {{GuestAddress{0x10280}, 0, 255}};
        CHECK_THROWS_AS(ogplay::loader::ApplyElf32ArmRelocations(
                            table, symbols, GuestAddress{0x20000}, fixture.plan,
                            fixture.memory),
                        ogplay::loader::RelocationError);
    }
    SUBCASE("duplicate target") {
        ogplay::loader::Elf32RelocationTable table;
        table.relocations = {
            {GuestAddress{0x10280}, 0, ogplay::loader::kArmRelocationRelative},
            {GuestAddress{0x10280}, 0, ogplay::loader::kArmRelocationRelative}};
        CHECK_THROWS_AS(ogplay::loader::ApplyElf32ArmRelocations(
                            table, symbols, GuestAddress{0x20000}, fixture.plan,
                            fixture.memory),
                        ogplay::loader::RelocationError);
    }
    CHECK(ReadWord(fixture.memory, GuestAddress{0x30280}) == 4);
    const std::array byte{std::byte{1}};
    CHECK_THROWS_AS(fixture.memory.Write(GuestAddress{0x30280}, byte),
                    ogplay::memory::MemoryFault);
}
