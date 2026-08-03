#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "ogplay/memory/address_space.h"

TEST_CASE("guest address space reserves 4 GiB and enforces the low guard") {
    ogplay::memory::AddressSpace memory;
    CHECK(memory.ReservedSize() == ogplay::memory::kGuestAddressSpaceSize);
    CHECK((memory.PageSize() & (memory.PageSize() - 1U)) == 0);
    CHECK_THROWS_AS(memory.Map(ogplay::memory::LowAddressGuard(),
                               ogplay::memory::PageProtection::read),
                    std::invalid_argument);
}

TEST_CASE("guest mappings support copy protect fault and zeroed remap") {
    ogplay::memory::AddressSpace memory;
    const auto page = memory.PageSize();
    const ogplay::memory::GuestAddress start{0x10000};
    const ogplay::memory::GuestRange range{start, page};
    memory.Map(range, ogplay::memory::PageProtection::read |
                          ogplay::memory::PageProtection::write);

    const std::array input{std::byte{0x12}, std::byte{0x34}, std::byte{0x56}};
    memory.Write(start.Add(7), input, 41);
    std::array<std::byte, input.size()> output{};
    memory.Read(start.Add(7), output, 41);
    CHECK(output == input);
    CHECK_THROWS_AS(memory.Map(range, ogplay::memory::PageProtection::read),
                    std::logic_error);

    memory.Protect(range, ogplay::memory::PageProtection::read);
    try {
        memory.Write(start, input, 77);
        FAIL("write to read-only guest memory did not fault");
    } catch (const ogplay::memory::MemoryFault& fault) {
        CHECK(fault.Address() == start);
        CHECK(fault.Access() == ogplay::memory::AccessType::write);
        CHECK(fault.Reason() == ogplay::memory::FaultReason::permission_denied);
        CHECK(fault.ThreadId() == 77);
    }

    memory.Unmap(range);
    CHECK_THROWS_AS(memory.Read(start, output), ogplay::memory::MemoryFault);
    memory.Map(range, ogplay::memory::PageProtection::read |
                          ogplay::memory::PageProtection::write);
    memory.Read(start.Add(7), output);
    CHECK(output == std::array<std::byte, input.size()>{});
}

TEST_CASE("guest mapping validates alignment permissions and the final page") {
    ogplay::memory::AddressSpace memory;
    const auto page = memory.PageSize();
    CHECK_THROWS_AS(
        memory.Map(ogplay::memory::GuestRange(ogplay::memory::GuestAddress{0x10001}, page),
                   ogplay::memory::PageProtection::read),
        std::invalid_argument);
    CHECK_THROWS_AS(
        memory.Map(ogplay::memory::GuestRange(ogplay::memory::GuestAddress{0x10000}, page),
                   ogplay::memory::PageProtection::write),
        std::invalid_argument);

    const auto final_value = ogplay::memory::kGuestAddressSpaceSize - page;
    const ogplay::memory::GuestAddress final_address{
        static_cast<std::uint32_t>(final_value)};
    const ogplay::memory::GuestRange final_page{final_address, page};
    memory.Map(final_page, ogplay::memory::PageProtection::read |
                             ogplay::memory::PageProtection::write);
    const std::array marker{std::byte{0x7f}};
    memory.Write(ogplay::memory::GuestAddress{0xffffffff}, marker);
    std::array<std::byte, 1> output{};
    memory.Read(ogplay::memory::GuestAddress{0xffffffff}, output);
    CHECK(output == marker);
}
