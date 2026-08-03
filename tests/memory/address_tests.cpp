#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "ogplay/memory/address.h"

static_assert(!std::is_convertible_v<std::uint32_t, ogplay::memory::GuestAddress>);
static_assert(sizeof(ogplay::memory::GuestAddress) == sizeof(std::uint32_t));

TEST_CASE("guest address arithmetic rejects wraparound") {
    const ogplay::memory::GuestAddress base{0x1000};
    CHECK(base.Add(0x234).Value() == 0x1234);
    CHECK(base.Subtract(1).Value() == 0x0fff);
    CHECK(base.OffsetTo(ogplay::memory::GuestAddress{0x1800}) == 0x800);
    CHECK_THROWS_AS(static_cast<void>(base.Subtract(0x1001)), std::overflow_error);
    CHECK_THROWS_AS(static_cast<void>(ogplay::memory::GuestAddress{0xffffffff}.Add(1)),
                    std::overflow_error);
    CHECK_THROWS_AS(static_cast<void>(
                        base.OffsetTo(ogplay::memory::GuestAddress{0x0fff})),
                    std::invalid_argument);
}

TEST_CASE("guest range represents the complete 32-bit address space") {
    const ogplay::memory::GuestRange entire{
        ogplay::memory::GuestAddress{}, ogplay::memory::kGuestAddressSpaceSize};
    CHECK(entire.EndExclusive() == UINT64_C(0x100000000));
    CHECK(entire.Contains(ogplay::memory::GuestAddress{0xffffffff}));

    const ogplay::memory::GuestRange last_byte{
        ogplay::memory::GuestAddress{0xffffffff}, 1};
    CHECK(entire.Contains(last_byte));
    CHECK_THROWS_AS(
        ogplay::memory::GuestRange(ogplay::memory::GuestAddress{0xffffffff}, 2),
        std::overflow_error);
    CHECK_THROWS_AS(ogplay::memory::GuestRange(ogplay::memory::GuestAddress{}, 0),
                    std::invalid_argument);
}

TEST_CASE("guest ranges use half-open containment and overlap") {
    const ogplay::memory::GuestRange first{ogplay::memory::GuestAddress{0x1000}, 0x1000};
    const ogplay::memory::GuestRange inside{ogplay::memory::GuestAddress{0x1800}, 0x100};
    const ogplay::memory::GuestRange adjacent{ogplay::memory::GuestAddress{0x2000}, 0x1000};
    CHECK(first.Contains(ogplay::memory::GuestAddress{0x1000}));
    CHECK_FALSE(first.Contains(ogplay::memory::GuestAddress{0x2000}));
    CHECK(first.Contains(inside));
    CHECK(first.Overlaps(inside));
    CHECK_FALSE(first.Overlaps(adjacent));
    CHECK(ogplay::memory::LowAddressGuard().EndExclusive() == 0x10000);
}

TEST_CASE("guest address alignment validates powers of two and the top boundary") {
    const ogplay::memory::GuestAddress address{0x12345};
    CHECK(address.AlignDown(0x1000).Value() == 0x12000);
    CHECK(address.AlignUp(0x1000).Value() == 0x13000);
    CHECK(address.AlignDown(0x1000).IsAligned(0x1000));
    CHECK_THROWS_AS(static_cast<void>(address.AlignUp(3)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(
                        ogplay::memory::GuestAddress{0xffffffff}.AlignUp(0x1000)),
                    std::overflow_error);
}
