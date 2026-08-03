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
