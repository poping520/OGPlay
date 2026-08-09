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
