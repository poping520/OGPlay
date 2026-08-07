#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include <doctest/doctest.h>

#include "ogplay/runtime/integration/api19_guest_process.h"

namespace {

constexpr ogplay::memory::GuestAddress kLibcAddress{0x10000000U};
constexpr ogplay::memory::GuestAddress kPropertyExportAddress{0x10000080U};

[[nodiscard]] std::uint32_t Read32(
    ogplay::memory::MemoryBus& bus,
    const ogplay::memory::GuestAddress address) {
    return bus.Read32(address, 1);
}

[[nodiscard]] ogplay::loader::Elf32LinkNamespace MakeNamespace() {
    ogplay::loader::Elf32LinkModule libc;
    libc.name = "libc.so";
    libc.load_bias = kLibcAddress;
    libc.symbols.symbols.push_back(
        {"", ogplay::memory::GuestAddress{0}, 0, 0, 0, 0, 0});
    libc.symbols.symbols.push_back(
        {"__system_property_area__", ogplay::memory::GuestAddress{0x80U},
         4, 1, 1, 0, 1});
    ogplay::loader::Elf32LinkNamespace result;
    result.modules.push_back(std::move(libc));
    result.load_order.push_back(0);
    result.lookup_scope.push_back(0);
    return result;
}

void MapLibcExport(ogplay::memory::AddressSpace& address_space) {
    address_space.Map(
        {kLibcAddress, address_space.PageSize()},
        ogplay::memory::PageProtection::read |
            ogplay::memory::PageProtection::write);
}

}  // namespace

TEST_CASE("API 19 guest process publishes complete reusable startup memory") {
    ogplay::memory::AddressSpace address_space;
    ogplay::memory::CheckedMemoryBus bus(address_space);
    MapLibcExport(address_space);
    bus.Write32(kPropertyExportAddress, 0xaabbccddU, 1);

    const auto process = ogplay::runtime::InitializeApi19GuestProcess(
        address_space, bus, MakeNamespace(), {1, "ogplay-profile"});

    CHECK(process.root_thread_id == 1);
    CHECK(process.thread_pointer == ogplay::runtime::kApi19GuestTlsAddress);
    CHECK(process.stack_top ==
          ogplay::runtime::kApi19GuestStackAddress.Add(
              ogplay::runtime::kApi19GuestStackSize - 64U));
    CHECK(process.return_trap ==
          ogplay::runtime::kApi19GuestReturnAddress);
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestThreadInfoAddress.Add(12)) ==
          ogplay::runtime::kApi19GuestStackAddress.Value());
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestThreadInfoAddress.Add(16)) ==
          ogplay::runtime::kApi19GuestStackSize);
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestThreadInfoAddress.Add(32)) ==
          1U);
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestThreadInfoAddress.Add(60)) ==
          ogplay::runtime::kApi19GuestTlsAddress.Value());
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestTlsAddress) ==
          ogplay::runtime::kApi19GuestTlsAddress.Value());
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestTlsAddress.Add(4)) ==
          ogplay::runtime::kApi19GuestThreadInfoAddress.Value());
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestTlsAddress.Add(12)) ==
          ogplay::runtime::kApi19GuestPreinitAddress.Value());
    CHECK(Read32(bus, kPropertyExportAddress) ==
          ogplay::runtime::kApi19GuestPropertyAreaAddress.Value());
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestPropertyAreaAddress) == 20U);
    CHECK(Read32(bus,
                 ogplay::runtime::kApi19GuestPropertyAreaAddress.Add(8)) ==
          0x504f5250U);

    std::array<std::byte, 4> trap{};
    address_space.Fetch(ogplay::runtime::kApi19GuestReturnAddress, trap, 1);
    CHECK(trap == std::array<std::byte, 4>{
                      std::byte{0x01}, std::byte{0x00},
                      std::byte{0x00}, std::byte{0xef}});
    const std::array<std::byte, 1> value{std::byte{0xff}};
    CHECK_THROWS_AS(
        address_space.Write(ogplay::runtime::kApi19GuestReturnAddress,
                            value, 1),
        ogplay::memory::MemoryFault);

    std::array<std::byte, 14> name{};
    address_space.Read(
        ogplay::runtime::kApi19GuestPreinitAddress.Add(0xa0), name, 1);
    CHECK(std::string(reinterpret_cast<const char*>(name.data()),
                      name.size()) == "ogplay-profile");
}

TEST_CASE("API 19 guest process rolls back a late fixed-layout collision") {
    ogplay::memory::AddressSpace address_space;
    ogplay::memory::CheckedMemoryBus bus(address_space);
    MapLibcExport(address_space);
    bus.Write32(kPropertyExportAddress, 0xaabbccddU, 1);
    const ogplay::memory::GuestRange occupied{
        ogplay::runtime::kApi19GuestPropertyAreaAddress,
        address_space.PageSize()};
    address_space.Map(occupied, ogplay::memory::PageProtection::read);

    CHECK_THROWS(static_cast<void>(
        ogplay::runtime::InitializeApi19GuestProcess(
            address_space, bus, MakeNamespace(), {1, "ogplay-profile"})));
    CHECK(Read32(bus, kPropertyExportAddress) == 0xaabbccddU);
    address_space.ValidateMapped(occupied);
    CHECK_THROWS_AS(
        address_space.ValidateMapped(
            {ogplay::runtime::kApi19GuestThreadInfoAddress,
             address_space.PageSize()}),
        ogplay::memory::MemoryFault);
    CHECK_THROWS_AS(
        address_space.ValidateMapped(
            {ogplay::runtime::kApi19GuestTlsAddress,
             address_space.PageSize()}),
        ogplay::memory::MemoryFault);
    CHECK_THROWS_AS(
        address_space.ValidateMapped(
            {ogplay::runtime::kApi19GuestStackAddress,
             ogplay::runtime::kApi19GuestStackSize}),
        ogplay::memory::MemoryFault);
}

TEST_CASE("API 19 guest process rejects invalid identity before mapping") {
    ogplay::memory::AddressSpace address_space;
    ogplay::memory::CheckedMemoryBus bus(address_space);
    MapLibcExport(address_space);
    const std::string oversized_name(32, 'x');

    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::InitializeApi19GuestProcess(
            address_space, bus, MakeNamespace(), {0, "ogplay"})),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::InitializeApi19GuestProcess(
            address_space, bus, MakeNamespace(), {1, oversized_name})),
        std::invalid_argument);
    CHECK_THROWS_AS(
        address_space.ValidateMapped(
            {ogplay::runtime::kApi19GuestThreadInfoAddress,
             address_space.PageSize()}),
        ogplay::memory::MemoryFault);
}
