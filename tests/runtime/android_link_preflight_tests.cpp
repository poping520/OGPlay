#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ogplay/runtime/integration/android_link_preflight.h"

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

std::vector<std::byte> MinimalModule() {
    std::vector<std::byte> bytes(0x240, std::byte{});
    bytes[0] = std::byte{0x7f}; bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'}; bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{1}; bytes[5] = std::byte{1}; bytes[6] = std::byte{1};
    Put16(bytes, 16, 3); Put16(bytes, 18, 40); Put32(bytes, 20, 1);
    Put32(bytes, 28, 52); Put32(bytes, 36, 0x05000400U);
    Put16(bytes, 40, 52); Put16(bytes, 42, 32); Put16(bytes, 44, 2);
    Put32(bytes, 52, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 60, 0x10000U); Put32(bytes, 68, 0x240);
    Put32(bytes, 72, 0x240); Put32(bytes, 76, 5); Put32(bytes, 80, 0x1000);
    Put32(bytes, 84, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 88, 0x100); Put32(bytes, 92, 0x10100U);
    Put32(bytes, 100, 56); Put32(bytes, 104, 56); Put32(bytes, 108, 6);
    Put32(bytes, 112, 4);
    Put32(bytes, 0x100, ogplay::loader::kElfDynamicStringTable);
    Put32(bytes, 0x104, 0x10160U);
    Put32(bytes, 0x108, ogplay::loader::kElfDynamicStringTableSize);
    Put32(bytes, 0x10c, 12);
    Put32(bytes, 0x110, ogplay::loader::kElfDynamicSoname);
    Put32(bytes, 0x114, 1);
    Put32(bytes, 0x118, ogplay::loader::kElfDynamicHash);
    Put32(bytes, 0x11c, 0x10180U);
    Put32(bytes, 0x120, ogplay::loader::kElfDynamicSymbolTable);
    Put32(bytes, 0x124, 0x101a0U);
    Put32(bytes, 0x128, ogplay::loader::kElfDynamicSymbolEntrySize);
    Put32(bytes, 0x12c, 16);
    const char strings[] = "\0sample.so\0";
    for (std::size_t index = 0; index < sizeof(strings); ++index) {
        bytes[0x160 + index] = static_cast<std::byte>(strings[index]);
    }
    Put32(bytes, 0x180, 1); Put32(bytes, 0x184, 1);
    Put32(bytes, 0x188, 0); Put32(bytes, 0x18c, 0);
    return bytes;
}

}  // namespace

TEST_CASE("Android link preflight maps and relocates without executing guest") {
    const auto bytes = MinimalModule();
    const ogplay::loader::Elf32ModuleInput module{
        "sample.so", bytes, ogplay::memory::GuestAddress{0x10000000U}};
    const auto report = ogplay::runtime::PreflightAndroidGuestLink(
        {19, "sample.so", std::span{&module, 1}});
    CHECK(report.guest_modules == 1);
    CHECK(report.boundary_modules == 0);
    CHECK(report.relocations == 0);
    CHECK_THROWS_AS(static_cast<void>(ogplay::runtime::PreflightAndroidGuestLink(
                        {19, "", {}})),
                    ogplay::runtime::AndroidLinkPreflightError);
}
