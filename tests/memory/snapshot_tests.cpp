#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <stdexcept>

#include "ogplay/memory/address_space.h"

TEST_CASE("memory snapshot restores mapped bytes and permissions") {
    ogplay::memory::AddressSpace memory;
    const auto page = memory.PageSize();
    const ogplay::memory::GuestAddress first{0x10000};
    const auto second = first.Add(page);
    const auto third = second.Add(page);
    const auto writable = ogplay::memory::PageProtection::read |
                          ogplay::memory::PageProtection::write;
    memory.Map(ogplay::memory::GuestRange(first, page), writable);
    memory.Map(ogplay::memory::GuestRange(second, page), writable);
    memory.Map(ogplay::memory::GuestRange(third, page),
               ogplay::memory::PageProtection::read);

    const std::array marker{std::byte{0x12}, std::byte{0x34}, std::byte{0x56}};
    memory.Write(second.Add(9), marker);
    const auto snapshot = memory.CaptureSnapshot();
    REQUIRE(snapshot.mappings.size() == 2);
    CHECK(snapshot.version == ogplay::memory::kMemorySnapshotVersion);
    CHECK(snapshot.page_size == page);
    CHECK(snapshot.mappings.front().range ==
          ogplay::memory::GuestRange(first, page * 2U));

    const std::array changed{std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}};
    memory.Write(second.Add(9), changed);
    memory.Unmap(ogplay::memory::GuestRange(third, page));
    memory.RestoreSnapshot(snapshot);

    std::array<std::byte, marker.size()> output{};
    memory.Read(second.Add(9), output);
    CHECK(output == marker);
    CHECK_THROWS_AS(memory.Write(third, marker), ogplay::memory::MemoryFault);
}

TEST_CASE("invalid memory snapshot leaves the current address space unchanged") {
    ogplay::memory::AddressSpace memory;
    const auto page = memory.PageSize();
    const ogplay::memory::GuestAddress start{0x10000};
    memory.Map(ogplay::memory::GuestRange(start, page),
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    const std::array marker{std::byte{0x7d}};
    memory.Write(start, marker);

    const auto valid = memory.CaptureSnapshot();
    auto invalid_version = valid;
    invalid_version.version += 1;
    CHECK_THROWS_AS(memory.RestoreSnapshot(invalid_version), std::invalid_argument);

    auto invalid_page_size = valid;
    invalid_page_size.page_size *= 2;
    CHECK_THROWS_AS(memory.RestoreSnapshot(invalid_page_size), std::invalid_argument);

    auto invalid_data = valid;
    invalid_data.mappings.front().data.pop_back();
    CHECK_THROWS_AS(memory.RestoreSnapshot(invalid_data), std::invalid_argument);

    auto invalid_range = valid;
    invalid_range.mappings.front().range = ogplay::memory::LowAddressGuard();
    CHECK_THROWS_AS(memory.RestoreSnapshot(invalid_range), std::invalid_argument);

    std::array<std::byte, 1> output{};
    memory.Read(start, output);
    CHECK(output == marker);
}

TEST_CASE("empty memory snapshot clears all mappings") {
    ogplay::memory::AddressSpace memory;
    const auto page = memory.PageSize();
    const ogplay::memory::GuestAddress start{0x10000};
    memory.Map(ogplay::memory::GuestRange(start, page),
               ogplay::memory::PageProtection::read);

    ogplay::memory::MemorySnapshot empty;
    empty.page_size = page;
    memory.RestoreSnapshot(empty);

    std::array<std::byte, 1> output{};
    CHECK_THROWS_AS(memory.Read(start, output), ogplay::memory::MemoryFault);
}
