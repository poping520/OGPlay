#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "ogplay/memory/address_space.h"

TEST_CASE("guest address space reserves 4 GiB and enforces the low guard") {
    ogplay::memory::AddressSpace memory;
    CHECK(memory.ReservedSize() == ogplay::memory::kGuestAddressSpaceSize);
    CHECK(memory.PageSize() == 4096);
    CHECK_THROWS_AS(memory.Map(ogplay::memory::LowAddressGuard(),
                               ogplay::memory::PageProtection::read),
                    std::invalid_argument);
}

TEST_CASE("adjacent guest pages remain independent within host backing") {
    ogplay::memory::AddressSpace memory;
    const auto page = memory.PageSize();
    const ogplay::memory::GuestAddress first{0x10000};
    const auto second = first.Add(page);
    const ogplay::memory::GuestRange first_range{first, page};
    const ogplay::memory::GuestRange second_range{second, page};
    const auto writable = ogplay::memory::PageProtection::read |
                          ogplay::memory::PageProtection::write;

    memory.Map(first_range, writable);
    memory.Map(second_range, writable);
    const std::array first_marker{std::byte{0x11}};
    const std::array second_marker{std::byte{0x22}};
    memory.Write(first, first_marker);
    memory.Write(second, second_marker);
    memory.Protect(second_range, ogplay::memory::PageProtection::read |
                                     ogplay::memory::PageProtection::execute);

    memory.Unmap(first_range);
    std::array<std::byte, 1> output{};
    memory.Fetch(second, output);
    CHECK(output == second_marker);
    CHECK_THROWS_AS(memory.Write(second, first_marker),
                    ogplay::memory::MemoryFault);
    CHECK_THROWS_AS(memory.Read(first, output), ogplay::memory::MemoryFault);

    memory.Map(first_range, writable);
    memory.Read(first, output);
    CHECK(output == std::array<std::byte, 1>{});
    memory.Read(second, output);
    CHECK(output == second_marker);
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

    memory.Protect(range, ogplay::memory::PageProtection::none);
    CHECK_NOTHROW(memory.ValidateMapped(range, 78));
    try {
        memory.Read(start, output, 78);
        FAIL("read from PROT_NONE guest memory did not fault");
    } catch (const ogplay::memory::MemoryFault& fault) {
        CHECK(fault.Reason() ==
              ogplay::memory::FaultReason::permission_denied);
        CHECK(fault.ThreadId() == 78);
    }
    CHECK_THROWS_AS(memory.Map(range, ogplay::memory::PageProtection::read),
                    std::logic_error);
    const auto protected_snapshot = memory.CaptureSnapshot();
    memory.Protect(range, ogplay::memory::PageProtection::read);
    memory.RestoreSnapshot(protected_snapshot);
    CHECK_THROWS_AS(memory.Read(start, output),
                    ogplay::memory::MemoryFault);
    memory.Protect(range, ogplay::memory::PageProtection::read);

    memory.Unmap(range);
    CHECK_THROWS_AS(memory.ValidateMapped(range),
                    ogplay::memory::MemoryFault);
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

TEST_CASE("guest C string scan is page aware bounded and fault precise") {
    ogplay::memory::AddressSpace memory;
    const auto page = memory.PageSize();
    const ogplay::memory::GuestAddress first{0x30000U};
    const auto second = first.Add(page);
    const auto writable = ogplay::memory::PageProtection::read |
                          ogplay::memory::PageProtection::write;
    memory.Map({first, page}, writable);
    memory.Map({second, page}, writable);

    const auto crossing = first.Add(page - 2U);
    const std::array text{
        std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{}};
    memory.Write(crossing, text, 91);
    CHECK(memory.CStringLength(crossing, text.size(), 91) == 3U);
    memory.Write8(first.Add(17U), 0U, 91);
    CHECK(memory.CStringLength(first.Add(17U), 1U, 91) == 0U);

    memory.Protect({second, page}, ogplay::memory::PageProtection::none);
    CHECK_THROWS_AS(static_cast<void>(
                        memory.CStringLength(crossing, 2U, 92)),
                    std::length_error);
    try {
        static_cast<void>(memory.CStringLength(crossing, 4U, 93));
        FAIL("cross-page C string scan did not fault");
    } catch (const ogplay::memory::MemoryFault& fault) {
        CHECK(fault.Address() == second);
        CHECK(fault.Access() == ogplay::memory::AccessType::read);
        CHECK(fault.Reason() ==
              ogplay::memory::FaultReason::permission_denied);
        CHECK(fault.ThreadId() == 93U);
    }
    CHECK_THROWS_AS(static_cast<void>(memory.CStringLength(first, 0U, 94)),
                    std::length_error);
}
