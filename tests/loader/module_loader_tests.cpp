#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ogplay/loader/module_loader.h"

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

[[nodiscard]] std::vector<std::byte> ModuleElf() {
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
    Put32(bytes, 28, 52);
    Put32(bytes, 36, 0x05000400U);
    Put16(bytes, 40, 52);
    Put16(bytes, 42, 32);
    Put16(bytes, 44, 2);
    Put32(bytes, 52, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 56, 0);
    Put32(bytes, 60, 0x10000U);
    Put32(bytes, 68, 0x300);
    Put32(bytes, 72, 0x300);
    Put32(bytes, 76, 5);
    Put32(bytes, 80, 0x1000);
    Put32(bytes, 84, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 88, 0x100);
    Put32(bytes, 92, 0x10100U);
    Put32(bytes, 100, 56);
    Put32(bytes, 104, 56);
    Put32(bytes, 108, 6);
    Put32(bytes, 112, 4);
    Put32(bytes, 0x100, ogplay::loader::kElfDynamicStringTable);
    Put32(bytes, 0x104, 0x10160U);
    Put32(bytes, 0x108, ogplay::loader::kElfDynamicStringTableSize);
    Put32(bytes, 0x10c, 20);
    Put32(bytes, 0x110, ogplay::loader::kElfDynamicSoname);
    Put32(bytes, 0x114, 1);
    Put32(bytes, 0x118, ogplay::loader::kElfDynamicHash);
    Put32(bytes, 0x11c, 0x10190U);
    Put32(bytes, 0x120, ogplay::loader::kElfDynamicSymbolTable);
    Put32(bytes, 0x124, 0x101b0U);
    Put32(bytes, 0x128, ogplay::loader::kElfDynamicSymbolEntrySize);
    Put32(bytes, 0x12c, 16);
    const char strings[] = "\0sample.so\0entry\0";
    for (std::size_t index = 0; index < sizeof(strings); ++index) {
        bytes[0x160 + index] = static_cast<std::byte>(strings[index]);
    }
    Put32(bytes, 0x190, 1);
    Put32(bytes, 0x194, 2);
    Put32(bytes, 0x198, 1);
    Put32(bytes, 0x19c, 0);
    Put32(bytes, 0x1c0, 11);
    Put32(bytes, 0x1c4, 0x10200U);
    Put32(bytes, 0x1c8, 4);
    bytes[0x1cc] = std::byte{0x12};
    Put16(bytes, 0x1ce, 1);
    Put32(bytes, 0x200, 0xe12fff1eU);
    return bytes;
}

[[nodiscard]] std::vector<std::byte> RelocationFailureElf() {
    auto bytes = ModuleElf();
    Put32(bytes, 100, 104);
    Put32(bytes, 104, 104);
    Put32(bytes, 0x130, ogplay::loader::kElfDynamicRel);
    Put32(bytes, 0x134, 0x10220U);
    Put32(bytes, 0x138, ogplay::loader::kElfDynamicRelSize);
    Put32(bytes, 0x13c, 8);
    Put32(bytes, 0x140, ogplay::loader::kElfDynamicRelEntrySize);
    Put32(bytes, 0x144, 8);
    Put32(bytes, 0x220, 0x10200U);
    Put32(bytes, 0x224, 255);
    return bytes;
}

}  // namespace

TEST_CASE("ELF module loader maps links and resolves one namespace transaction") {
    const auto bytes = ModuleElf();
    const ogplay::loader::Elf32ModuleInput input{
        "sample.so", bytes, ogplay::memory::GuestAddress{0x20000U}};
    ogplay::memory::AddressSpace memory;
    const auto loaded = ogplay::loader::LoadElf32ModuleNamespace(
        "sample.so", std::span{&input, 1}, memory);
    CHECK(loaded.modules.size() == 1);
    CHECK(ogplay::loader::LookupElf32Symbol(
              loaded.link_namespace, "entry").address ==
          ogplay::memory::GuestAddress{0x30200U});
    std::vector<std::byte> instruction(4);
    memory.Fetch(ogplay::memory::GuestAddress{0x30200U}, instruction);
    CHECK(instruction[0] == std::byte{0x1e});
}

TEST_CASE("ELF module loader restores the prior address space on failure") {
    const auto bytes = RelocationFailureElf();
    const ogplay::loader::Elf32ModuleInput input{
        "sample.so", bytes, ogplay::memory::GuestAddress{0x20000U}};
    ogplay::memory::AddressSpace memory;
    CHECK_THROWS(static_cast<void>(
        ogplay::loader::LoadElf32ModuleNamespace(
            "sample.so", std::span{&input, 1}, memory)));
    CHECK_THROWS_AS(memory.Validate(
                        {ogplay::memory::GuestAddress{0x30000U}, 4},
                        ogplay::memory::AccessType::read),
                    ogplay::memory::MemoryFault);
}

TEST_CASE("ELF module loader accepts appended absolute HLE boundary modules") {
    const auto bytes = ModuleElf();
    const ogplay::loader::Elf32ModuleInput input{
        "sample.so", bytes, ogplay::memory::GuestAddress{0x20000U}};
    ogplay::memory::AddressSpace memory;
    const auto loaded = ogplay::loader::LoadElf32ModuleNamespace(
        "sample.so", std::span{&input, 1}, memory,
        [](const std::string_view,
           const std::span<const ogplay::loader::Elf32LinkModule> guest) {
            auto root = guest.front();
            ogplay::loader::Elf32LinkModule boundary;
            boundary.name = "libhost.so";
            boundary.dynamic.soname = boundary.name;
            boundary.symbols.symbols.push_back(
                {"", ogplay::memory::GuestAddress{0}, 0, 0, 0, 0, 0});
            boundary.symbols.symbols.push_back(
                {"host_call", ogplay::memory::GuestAddress{0x70001001U},
                 2, 1, 2, 0, 0xfff1});
            return ogplay::loader::Elf32LinkNamespace{
                {std::move(root), std::move(boundary)}, {1, 0}, {0, 1}};
        });
    CHECK(loaded.modules.size() == 1);
    CHECK(loaded.link_namespace.modules.size() == 2);
    CHECK(ogplay::loader::LookupElf32Symbol(
              loaded.link_namespace, "host_call").address ==
          ogplay::memory::GuestAddress{0x70001001U});
    std::vector<std::byte> instruction(4);
    memory.Fetch(ogplay::memory::GuestAddress{0x30200U}, instruction);
    CHECK(instruction[0] == std::byte{0x1e});
}

TEST_CASE("ELF module loader rejects builders that replace guest identity") {
    const auto bytes = ModuleElf();
    const ogplay::loader::Elf32ModuleInput input{
        "sample.so", bytes, ogplay::memory::GuestAddress{0x20000U}};
    ogplay::memory::AddressSpace memory;
    CHECK_THROWS_AS(static_cast<void>(
        ogplay::loader::LoadElf32ModuleNamespace(
            "sample.so", std::span{&input, 1}, memory,
            [](const std::string_view,
               const std::span<const ogplay::loader::Elf32LinkModule> guest) {
                auto changed = guest.front();
                changed.name = "replacement.so";
                return ogplay::loader::Elf32LinkNamespace{
                    {std::move(changed)}, {0}, {0}};
            })), ogplay::loader::LinkError);
}
