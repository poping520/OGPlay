#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"

namespace {

class RecordingObserver final : public ogplay::memory::MemoryAccessObserver {
public:
    void OnMemoryAccess(const ogplay::memory::BusAccess& access) override {
        accesses.push_back(access);
    }

    std::vector<ogplay::memory::BusAccess> accesses;
};

}  // namespace

TEST_CASE("checked memory bus uses explicit little-endian typed accesses") {
    ogplay::memory::AddressSpace space;
    const ogplay::memory::GuestAddress start{0x10000};
    space.Map({start, space.PageSize()}, ogplay::memory::PageProtection::read |
                                            ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus(space);
    bus.Write32(start, UINT32_C(0x78563412));
    CHECK(bus.Read8(start) == 0x12);
    CHECK(bus.Read16(start) == 0x3412);
    CHECK(bus.Read32(start) == UINT32_C(0x78563412));

    std::array<std::byte, 4> bytes{};
    space.Read(start, bytes);
    CHECK(bytes == std::array{std::byte{0x12}, std::byte{0x34},
                              std::byte{0x56}, std::byte{0x78}});
}

TEST_CASE("checked memory bus validates the full cross-page access before writing") {
    ogplay::memory::AddressSpace space;
    const auto page = space.PageSize();
    const ogplay::memory::GuestAddress start{0x10000};
    space.Map({start, page * 2U}, ogplay::memory::PageProtection::read |
                                       ogplay::memory::PageProtection::write);
    const ogplay::memory::GuestRange second_page{start.Add(page), page};
    space.Protect(second_page, ogplay::memory::PageProtection::read);
    ogplay::memory::CheckedMemoryBus bus(space);
    const auto crossing = start.Add(page - 2U);
    CHECK_THROWS_AS(bus.Write32(crossing, UINT32_C(0xffffffff), 9),
                    ogplay::memory::MemoryFault);
    CHECK(bus.Read16(crossing) == 0);
}

TEST_CASE("checked memory bus reports successful accesses to an explicit observer") {
    ogplay::memory::AddressSpace space;
    const ogplay::memory::GuestAddress start{0x10000};
    space.Map({start, space.PageSize()}, ogplay::memory::PageProtection::read |
                                            ogplay::memory::PageProtection::write);
    RecordingObserver observer;
    ogplay::memory::CheckedMemoryBus bus(space, &observer);
    bus.Write64(start, UINT64_C(0x0102030405060708), 55);
    CHECK(bus.Read64(start, 55) == UINT64_C(0x0102030405060708));
    REQUIRE(observer.accesses.size() == 2);
    CHECK(observer.accesses[0].type == ogplay::memory::BusAccessType::write);
    CHECK(observer.accesses[0].size == 8);
    CHECK(observer.accesses[0].thread_id == 55);
    CHECK(observer.accesses[1].type == ogplay::memory::BusAccessType::read);
}

TEST_CASE("direct page table publishes only unobserved non-executable RW pages") {
    ogplay::memory::AddressSpace space;
    const ogplay::memory::GuestAddress start{0x10000};
    const auto page_index = start.Value() >> ogplay::memory::kGuestPageBits;
    ogplay::memory::CheckedMemoryBus bus(space);
    auto* table = bus.DirectPageTable();
    REQUIRE(table != nullptr);
    CHECK((*table)[page_index] == nullptr);

    const auto read_write = ogplay::memory::PageProtection::read |
                            ogplay::memory::PageProtection::write;
    space.Map({start, space.PageSize()}, read_write);
    REQUIRE((*table)[page_index] != nullptr);
    bus.Write32(start, 0x78563412U);
    CHECK((*table)[page_index][0] == 0x12U);

    space.Protect({start, space.PageSize()}, ogplay::memory::PageProtection::read);
    CHECK((*table)[page_index] == nullptr);
    space.Protect({start, space.PageSize()},
                  read_write | ogplay::memory::PageProtection::execute);
    CHECK((*table)[page_index] == nullptr);
    space.Protect({start, space.PageSize()}, read_write);
    CHECK((*table)[page_index] != nullptr);
    space.Unmap({start, space.PageSize()});
    CHECK((*table)[page_index] == nullptr);

    RecordingObserver observer;
    ogplay::memory::CheckedMemoryBus observed(space, &observer);
    CHECK(observed.DirectPageTable() == nullptr);
}

TEST_CASE("read page table follows read permission independently of write and execute") {
    using namespace ogplay::memory;
    AddressSpace space;
    CheckedMemoryBus bus(space);
    const GuestAddress start{0x10000};
    const auto index = start.Value() >> kGuestPageBits;
    auto* reads = bus.DirectReadPageTable();
    auto* writes = bus.DirectPageTable();
    REQUIRE(reads != nullptr);
    REQUIRE(writes != nullptr);
    CHECK((*reads)[index] == nullptr);
    space.Map({start, space.PageSize()}, PageProtection::read | PageProtection::write);
    bus.Write32(start, 0x78563412U);
    for (const auto protection : {PageProtection::read,
            PageProtection::read | PageProtection::execute}) {
        space.Protect({start, space.PageSize()}, protection);
        REQUIRE((*reads)[index] != nullptr);
        CHECK((*reads)[index][0] == 0x12);
        CHECK((*writes)[index] == nullptr);
    }
    const auto snapshot = space.CaptureSnapshot();
    for (const auto protection : {PageProtection::none, PageProtection::execute}) {
        space.Protect({start, space.PageSize()}, protection);
        CHECK((*reads)[index] == nullptr);
    }
    space.Unmap({start, space.PageSize()});
    CHECK((*reads)[index] == nullptr);
    space.RestoreSnapshot(snapshot);
    CHECK(bus.DirectReadPageTable() == reads);
    REQUIRE((*reads)[index] != nullptr);
    CHECK((*reads)[index][0] == 0x12);
    CHECK((*writes)[index] == nullptr);
    RecordingObserver observer;
    CheckedMemoryBus observed(space, &observer);
    CHECK(observed.DirectReadPageTable() == nullptr);
}

TEST_CASE("instruction fetch uses execute permission independently from data reads") {
    RecordingObserver observer;
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress start{0x10000};
    memory.Map(ogplay::memory::GuestRange(start, memory.PageSize()),
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus(memory, &observer);
    bus.Write32(start, 0xe3a0002a, 18);
    memory.Protect(ogplay::memory::GuestRange(start, memory.PageSize()),
                   ogplay::memory::PageProtection::execute);

    CHECK(bus.Fetch16(start, 18) == 0x002a);
    CHECK(bus.Fetch32(start, 18) == 0xe3a0002a);
    CHECK_THROWS_AS(static_cast<void>(bus.Read8(start, 18)),
                    ogplay::memory::MemoryFault);
    REQUIRE(observer.accesses.size() == 3);
    CHECK(observer.accesses[1].type == ogplay::memory::BusAccessType::execute);
    CHECK(observer.accesses[2].type == ogplay::memory::BusAccessType::execute);

    memory.Protect(ogplay::memory::GuestRange(start, memory.PageSize()),
                   ogplay::memory::PageProtection::read);
    try {
        static_cast<void>(bus.Fetch32(start, 91));
        FAIL("fetch from a non-executable page did not fault");
    } catch (const ogplay::memory::MemoryFault& fault) {
        CHECK(fault.Access() == ogplay::memory::AccessType::execute);
        CHECK(fault.Reason() == ogplay::memory::FaultReason::permission_denied);
        CHECK(fault.ThreadId() == 91);
    }
}

TEST_CASE("checked memory atomic compare exchange preserves width and observer semantics") {
    using namespace ogplay::memory;
    AddressSpace space;
    const GuestAddress start{0x10000};
    space.Map({start, 4096}, PageProtection::read | PageProtection::write);
    RecordingObserver observer;
    CheckedMemoryBus bus(space, &observer);
    SUBCASE("8 bit") {
        const auto expected = static_cast<std::uint8_t>(0xfedcba9876543210ULL);
        const auto desired = static_cast<std::uint8_t>(0x123456789abcdef0ULL);
        bus.Write64(start, UINT64_C(0xeeeeeeeeeeeeeeee));
        bus.Write8(start, expected);
        observer.accesses.clear();
        CHECK_FALSE(bus.CompareExchange8(start, 0, desired, 83));
        REQUIRE(observer.accesses.size() == 1);
        CHECK(observer.accesses[0].type == BusAccessType::read);
        CHECK(bus.CompareExchange8(start, expected, desired, 83));
        REQUIRE(observer.accesses.size() == 3);
        CHECK(observer.accesses[2].type == BusAccessType::write);
        CHECK(observer.accesses[2].thread_id == 83);
        CHECK(observer.accesses[2].size == 1);
        CHECK(bus.Read8(start) == desired);
        for (std::uint64_t i = 0; i < 1; ++i) {
            CHECK(bus.Read8(start.Add(i)) == ((desired >> (i * 8U)) & 0xffU));
        }
        for (std::uint64_t i = 1; i < 8; ++i) {
            CHECK(bus.Read8(start.Add(i)) == 0xee);
        }
    }
    SUBCASE("16 bit") {
        const auto expected = static_cast<std::uint16_t>(0xfedcba9876543210ULL);
        const auto desired = static_cast<std::uint16_t>(0x123456789abcdef0ULL);
        bus.Write64(start, UINT64_C(0xeeeeeeeeeeeeeeee));
        bus.Write16(start, expected);
        observer.accesses.clear();
        CHECK_FALSE(bus.CompareExchange16(start, 0, desired, 83));
        REQUIRE(observer.accesses.size() == 1);
        CHECK(observer.accesses[0].type == BusAccessType::read);
        CHECK(bus.CompareExchange16(start, expected, desired, 83));
        REQUIRE(observer.accesses.size() == 3);
        CHECK(observer.accesses[2].type == BusAccessType::write);
        CHECK(observer.accesses[2].thread_id == 83);
        CHECK(observer.accesses[2].size == 2);
        CHECK(bus.Read16(start) == desired);
        for (std::uint64_t i = 0; i < 2; ++i) {
            CHECK(bus.Read8(start.Add(i)) == ((desired >> (i * 8U)) & 0xffU));
        }
        for (std::uint64_t i = 2; i < 8; ++i) {
            CHECK(bus.Read8(start.Add(i)) == 0xee);
        }
    }
    SUBCASE("32 bit") {
        const auto expected = static_cast<std::uint32_t>(0xfedcba9876543210ULL);
        const auto desired = static_cast<std::uint32_t>(0x123456789abcdef0ULL);
        bus.Write64(start, UINT64_C(0xeeeeeeeeeeeeeeee));
        bus.Write32(start, expected);
        observer.accesses.clear();
        CHECK_FALSE(bus.CompareExchange32(start, 0, desired, 83));
        REQUIRE(observer.accesses.size() == 1);
        CHECK(observer.accesses[0].type == BusAccessType::read);
        CHECK(bus.CompareExchange32(start, expected, desired, 83));
        REQUIRE(observer.accesses.size() == 3);
        CHECK(observer.accesses[2].type == BusAccessType::write);
        CHECK(observer.accesses[2].thread_id == 83);
        CHECK(observer.accesses[2].size == 4);
        CHECK(bus.Read32(start) == desired);
        for (std::uint64_t i = 0; i < 4; ++i) {
            CHECK(bus.Read8(start.Add(i)) == ((desired >> (i * 8U)) & 0xffU));
        }
        for (std::uint64_t i = 4; i < 8; ++i) {
            CHECK(bus.Read8(start.Add(i)) == 0xee);
        }
    }
    SUBCASE("64 bit") {
        const auto expected = static_cast<std::uint64_t>(0xfedcba9876543210ULL);
        const auto desired = static_cast<std::uint64_t>(0x123456789abcdef0ULL);
        bus.Write64(start, UINT64_C(0xeeeeeeeeeeeeeeee));
        bus.Write64(start, expected);
        observer.accesses.clear();
        CHECK_FALSE(bus.CompareExchange64(start, 0, desired, 83));
        REQUIRE(observer.accesses.size() == 1);
        CHECK(observer.accesses[0].type == BusAccessType::read);
        CHECK(bus.CompareExchange64(start, expected, desired, 83));
        REQUIRE(observer.accesses.size() == 3);
        CHECK(observer.accesses[2].type == BusAccessType::write);
        CHECK(observer.accesses[2].thread_id == 83);
        CHECK(observer.accesses[2].size == 8);
        CHECK(bus.Read64(start) == desired);
        for (std::uint64_t i = 0; i < 8; ++i) {
            CHECK(bus.Read8(start.Add(i)) == ((desired >> (i * 8U)) & 0xffU));
        }
        for (std::uint64_t i = 8; i < 8; ++i) {
            CHECK(bus.Read8(start.Add(i)) == 0xee);
        }
    }
}

TEST_CASE("checked memory atomic compare exchange validates permissions alignment and lifetime") {
    using namespace ogplay::memory;
    AddressSpace space;
    const GuestAddress start{0x10000};
    const auto rw = PageProtection::read | PageProtection::write;
    space.Map({start, 8192}, rw);
    RecordingObserver observer;
    CheckedMemoryBus bus(space, &observer);
    bus.Write64(start, 9);
    observer.accesses.clear();
    const auto check_fault = [&](GuestAddress address, FaultReason reason,
                                 AccessType access) {
        try {
            static_cast<void>(bus.CompareExchange64(address, 9, 10, 91));
            FAIL("atomic access unexpectedly completed");
        } catch (const MemoryFault& fault) {
            CHECK(fault.Address() == address);
            CHECK(fault.Reason() == reason);
            CHECK(fault.Access() == access);
            CHECK(fault.ThreadId() == 91);
        }
        CHECK(observer.accesses.empty());
    };
    check_fault(start.Add(1), FaultReason::misaligned, AccessType::write);
    check_fault(start.Add(4092), FaultReason::misaligned, AccessType::write);
    space.Protect({start, 4096}, PageProtection::read);
    check_fault(start, FaultReason::permission_denied, AccessType::write);
    space.Protect({start, 4096}, PageProtection::none);
    check_fault(start, FaultReason::permission_denied, AccessType::read);
    space.Unmap({start, 4096});
    check_fault(start, FaultReason::unmapped, AccessType::read);
    space.Map({start, 4096}, rw);
    CHECK_FALSE(bus.CompareExchange64(start, 9, 10, 91));
    CHECK(bus.CompareExchange64(start, 0, 10, 91));
    CHECK(bus.Read64(start) == 10);
}
