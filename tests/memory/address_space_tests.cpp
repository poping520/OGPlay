#include <doctest/doctest.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <set>

#include "ogplay/memory/address_space.h"

TEST_CASE("SBX-15 anonymous fixed replacement preserves neighbors across mappings and holes") {
    using namespace ogplay::memory;
    AddressSpace memory;
    constexpr std::uint64_t page = 4096;
    const GuestAddress base{0x10000};
    const auto rw = PageProtection::read | PageProtection::write;
    memory.Map({base, 2 * page}, rw);
    memory.Map({base.Add(3 * page), 3 * page}, rw);
    std::array<std::byte, page> marker;
    marker.fill(std::byte{0x5a});
    for (const auto i : {0U, 1U, 3U, 4U, 5U}) memory.Write(base.Add(i * page), marker);
    memory.Protect({base.Add(3 * page), page}, PageProtection::read | PageProtection::execute);
    memory.Protect({base.Add(4 * page), page}, PageProtection::none);
    const auto ticket = memory.PreflightWrite({base.Add(page), 1});
    auto* table = memory.DirectPageTable();
    const auto first_index = base.Value() >> kGuestPageBits;
    auto* neighbor = (*table)[first_index];
    const auto generation = memory.TrySnapshot()->generation;
    CHECK_THROWS_AS(memory.Map({base.Add(page), page}, rw), std::logic_error);

    memory.ReplaceAnonymous({base.Add(page), page}, PageProtection::read);
    std::array<std::byte, page> output;
    memory.Read(base.Add(page), output);
    CHECK(output == std::array<std::byte, page>{});
    memory.Read(base, output);
    CHECK(output == marker);
    memory.Read(base.Add(3 * page), output);
    CHECK(output == marker);
    CHECK((*table)[first_index] == neighbor);
    CHECK((*table)[first_index + 1] == nullptr);
    CHECK(memory.TrySnapshot()->generation == generation + 1);
    CHECK_THROWS_AS(memory.WritePrevalidated(ticket, std::span{marker}.first(1)), MemoryFault);
    CHECK_THROWS_AS(memory.Fetch(base.Add(page), output), MemoryFault);

    memory.ReplaceAnonymous({base.Add(page), 4 * page}, rw);
    for (std::uint64_t i = 1; i <= 4; ++i) {
        memory.Read(base.Add(i * page), output);
        CHECK(output == std::array<std::byte, page>{});
        CHECK((*table)[first_index + i] != nullptr);
    }
    for (const auto i : {0U, 5U}) {
        memory.Read(base.Add(i * page), output);
        CHECK(output == marker);
    }
    CHECK(memory.TrySnapshot()->generation == generation + 2);
    CHECK(memory.TrySnapshot()->pages_by_protection[3] == 6);
    CHECK_THROWS_AS(memory.Fetch(base.Add(3 * page), output), MemoryFault);
    CHECK_NOTHROW(memory.Write(base.Add(4 * page), marker));
}

TEST_CASE("SBX-15 anonymous fixed replacement validates before mutation") {
    using namespace ogplay::memory;
    AddressSpace memory;
    const GuestAddress base{0x10000};
    const auto rw = PageProtection::read | PageProtection::write;
    memory.Map({base, 8192}, rw);
    memory.Write32(base, 0x12345678);
    const auto before = memory.TrySnapshot();
    CHECK_THROWS_AS(memory.ReplaceAnonymous({base.Add(1), 4096}, rw), std::invalid_argument);
    CHECK_THROWS_AS(memory.ReplaceAnonymous({base, 4095}, rw), std::invalid_argument);
    CHECK_THROWS_AS(memory.ReplaceAnonymous({base, 0}, rw), std::invalid_argument);
    CHECK_THROWS_AS(memory.ReplaceAnonymous(LowAddressGuard(), rw), std::invalid_argument);
    CHECK_THROWS_AS(memory.ReplaceAnonymous({base, 4096}, PageProtection::write), std::invalid_argument);
    CHECK_THROWS_AS(memory.ReplaceAnonymous({base, 4096}, static_cast<PageProtection>(0x80)), std::invalid_argument);
    CHECK_THROWS_AS(memory.ReplaceAnonymous({GuestAddress{0xfffff000}, 8192}, rw), std::overflow_error);
    CHECK(memory.Read32(base) == 0x12345678);
    CHECK(memory.TrySnapshot()->generation == before->generation);
    CHECK(memory.TrySnapshot()->pages_by_protection == before->pages_by_protection);

    const GuestAddress last{0xfffff000};
    memory.ReplaceAnonymous({last, 4096}, rw);
    memory.Write8(GuestAddress{0xffffffff}, 0x7f);
    memory.ReplaceAnonymous({last, 4096}, PageProtection::read);
    CHECK(memory.Read8(GuestAddress{0xffffffff}) == 0);
}

TEST_CASE("SBX-15 anonymous fixed replacement is atomic to checked readers") {
    using namespace ogplay::memory;
    AddressSpace memory;
    const GuestAddress base{0x10000};
    const auto rw = PageProtection::read | PageProtection::write;
    memory.Map({base, 8192}, rw);
    std::array<std::byte, 8192> marker;
    marker.fill(std::byte{0x5a});
    std::atomic<bool> started{false}, done{false}, intact{true};
    std::thread reader([&] {
        std::array<std::byte, 8192> output;
        started = true;
        do {
            try {
                memory.Read(base, output);
                if (!std::all_of(output.begin(), output.end(), [&](const auto byte) {
                        return byte == output.front();
                    }) || (output.front() != std::byte{} && output.front() != marker.front())) {
                    intact = false;
                }
            } catch (...) {
                intact = false;
            }
        } while (!done);
    });
    while (!started) std::this_thread::yield();
    for (int i = 0; i < 500; ++i) {
        memory.Write(base, marker);
        memory.ReplaceAnonymous({base, marker.size()}, rw);
    }
    done = true;
    reader.join();
    CHECK(intact);
}

TEST_CASE("guest atomic first fit skips guards reuses holes and preserves failure state") {
    using namespace ogplay::memory;
    AddressSpace memory;
    const GuestRange bounds{GuestAddress{0x10000}, 5 * 4096};
    const auto rw = PageProtection::read | PageProtection::write;
    memory.Map({GuestAddress{0x11000}, 4096}, PageProtection::none);
    CHECK(memory.MapAnywhere(bounds, 8192, rw).Value() == 0x12000);
    CHECK(memory.MapAnywhere(bounds, 4096, rw).Value() == 0x10000);
    CHECK_THROWS_AS(static_cast<void>(memory.MapAnywhere(bounds, 8192, rw)), std::bad_alloc);
    CHECK_THROWS_AS(static_cast<void>(memory.MapAnywhere(bounds, 0, rw)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(memory.MapAnywhere(bounds, 1, rw)), std::invalid_argument);
    CHECK(memory.MapAnywhere(bounds, 4096, rw).Value() == 0x14000);
    memory.Write32(GuestAddress{0x12000}, 123);
    memory.Unmap({GuestAddress{0x12000}, 8192});
    CHECK(memory.MapAnywhere(bounds, 8192, rw).Value() == 0x12000);
    CHECK(memory.Read32(GuestAddress{0x12000}) == 0);
    CHECK_THROWS_AS(static_cast<void>(memory.Read8(GuestAddress{0x11000})), MemoryFault);
    CHECK(memory.MapAnywhere({GuestAddress{0xfffff000}, 4096}, 4096, rw).Value() == 0xfffff000);
    CHECK_THROWS_AS(static_cast<void>(memory.MapAnywhere(LowAddressGuard(), 4096, rw)),
                    std::bad_alloc);
}

TEST_CASE("guest atomic first fit serializes concurrent allocations") {
    using namespace ogplay::memory;
    AddressSpace memory;
    std::array<GuestAddress, 16> addresses;
    std::array<std::thread, 16> threads;
    for (std::size_t i = 0; i < threads.size(); ++i) {
        threads[i] = std::thread([&, i] {
            addresses[i] = memory.MapAnywhere({GuestAddress{0x10000}, 16 * 4096},
                                              4096, PageProtection::none);
        });
    }
    for (auto& thread : threads) thread.join();
    std::set<std::uint32_t> unique;
    for (const auto address : addresses) unique.insert(address.Value());
    CHECK(unique.size() == addresses.size());
}

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

TEST_CASE("VFS-05 mapping metadata tracks permissions replacement and holes") {
    using namespace ogplay::memory;
    AddressSpace space;
    space.Map({GuestAddress{0x10000}, 0x3000}, PageProtection::read | PageProtection::write);
    space.Protect({GuestAddress{0x11000}, 0x1000}, PageProtection::none);
    auto maps = space.DescribeMappings();
    REQUIRE(maps.size() == 3);
    CHECK(maps[1].range.Start().Value() == 0x11000);
    CHECK(maps[1].protection == PageProtection::none);
    CHECK_THROWS_AS(static_cast<void>(space.DescribeMappings(2)), std::length_error);
    space.Unmap({GuestAddress{0x11000}, 0x1000});
    CHECK(space.DescribeMappings().size() == 2);
    space.ReplaceAnonymous({GuestAddress{0x11000}, 0x1000}, PageProtection::read | PageProtection::write);
    maps = space.DescribeMappings();
    REQUIRE(maps.size() == 1);
    CHECK(maps[0].range.Size() == 0x3000);
    space.Map({GuestAddress{0xfffff000}, 0x1000}, PageProtection::execute);
    maps = space.DescribeMappings();
    CHECK(static_cast<std::uint64_t>(maps.back().range.Start().Value()) + maps.back().range.Size() == (UINT64_C(1) << 32U));
    AddressSpace other;
    CHECK(other.DescribeMappings().empty());
}

TEST_CASE("mapping subscriptions publish committed ranges and retire safely") {
    using namespace ogplay::memory;
    AddressSpace space;
    std::vector<GuestRange> changed;
    auto subscription = space.ObserveMappingChanges([&](GuestRange range) {
        static_cast<void>(space.DescribeMappings()); // No ledger lock held.
        changed.push_back(range);
    });
    const GuestAddress page{0x10000};
    const auto rw = PageProtection::read | PageProtection::write;
    space.Map({page, 4096}, rw);
    space.Write32(page, 1); // Ordinary writes still require guest cacheflush.
    CHECK(changed.size() == 1);
    CHECK_THROWS(space.Map({page, 4096}, rw));
    CHECK_THROWS(space.Protect({page.Add(4096), 4096}, PageProtection::none));
    CHECK(changed.size() == 1);
    space.Protect({page, 4096}, PageProtection::none);
    space.ReplaceAnonymous({page, 4096}, rw);
    const auto allocated = space.MapAnywhere({page, 8192}, 4096, rw);
    CHECK(allocated == page.Add(4096));
    const auto snapshot = space.CaptureSnapshot();
    space.Unmap({page, 4096});
    space.RestoreSnapshot(snapshot);
    REQUIRE(changed.size() == 6);
    CHECK(changed[0].Start() == page);
    CHECK(changed[3].Start() == allocated);
    CHECK(changed[5].Start() == GuestAddress{0});
    CHECK(changed[5].Size() == kGuestAddressSpaceSize);
    subscription.reset();
    space.Unmap({page, 8192});
    CHECK(changed.size() == 6);
    // Unsubscribing after the address space is gone is also safe.
    MappingChangeSubscription expired;
    { AddressSpace temporary; expired = temporary.ObserveMappingChanges([](GuestRange) {}); }
    expired.reset();
}
