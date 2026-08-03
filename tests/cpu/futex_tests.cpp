#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

#include "ogplay/cpu/futex.h"
#include "ogplay/hal/thread.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"

TEST_CASE("futex wait wake provides exact multi-thread synchronization") {
    constexpr std::size_t kThreadCount = 16;
    const ogplay::memory::GuestAddress address{0x20000};
    ogplay::memory::AddressSpace memory;
    memory.Map({address, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus(memory);
    bus.Write32(address, 7);
    ogplay::cpu::FutexTable futex;
    std::atomic_size_t awoken{};
    std::array<std::unique_ptr<ogplay::hal::HostThread>, kThreadCount> threads;

    for (std::size_t index = 0; index < threads.size(); ++index) {
        threads[index] = ogplay::hal::StartHostThread([&, index] {
            if (futex.Wait(bus, address, 7, index + 1) ==
                ogplay::cpu::FutexWaitResult::awoken) {
                awoken.fetch_add(1);
            }
        });
    }
    for (std::size_t attempt = 0;
         attempt < 100'000 && futex.WaiterCount(address) != kThreadCount;
         ++attempt) {
        std::this_thread::yield();
    }
    const auto all_waiting = futex.WaiterCount(address) == kThreadCount;
    if (!all_waiting) {
        static_cast<void>(futex.Wake(address,
                                     std::numeric_limits<std::size_t>::max()));
    }
    REQUIRE(all_waiting);

    bus.Write32(address, 8);
    CHECK(futex.Wake(address, 5) == 5);
    for (std::size_t attempt = 0; attempt < 100'000 && awoken.load() != 5;
         ++attempt) {
        std::this_thread::yield();
    }
    CHECK(awoken.load() == 5);
    CHECK(futex.Wake(address, std::numeric_limits<std::size_t>::max()) == 11);
    for (auto& thread : threads) thread->Join();
    CHECK(awoken.load() == kThreadCount);
    CHECK(futex.WaiterCount(address) == 0);
}

TEST_CASE("futex mismatch and invalid addresses fail without sleeping") {
    const ogplay::memory::GuestAddress address{0x20000};
    ogplay::memory::AddressSpace memory;
    memory.Map({address, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus(memory);
    bus.Write32(address, 9);
    ogplay::cpu::FutexTable futex;

    CHECK(futex.Wait(bus, address, 8, 77) ==
          ogplay::cpu::FutexWaitResult::value_mismatch);
    CHECK(futex.WaiterCount(address) == 0);
    CHECK(futex.Wake(address, 1) == 0);
    const auto wait = [&](const ogplay::memory::GuestAddress wait_address) {
        static_cast<void>(futex.Wait(bus, wait_address, 9, 77));
    };
    const auto wake = [&](const ogplay::memory::GuestAddress wake_address) {
        static_cast<void>(futex.Wake(wake_address, 1));
    };
    CHECK_THROWS_AS(wait(address.Add(1)), std::invalid_argument);
    CHECK_THROWS_AS(wake(address.Add(2)), std::invalid_argument);
    CHECK_THROWS_AS(wait(ogplay::memory::GuestAddress{0x30000}),
                    ogplay::memory::MemoryFault);
}
