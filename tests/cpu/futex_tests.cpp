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

TEST_CASE("futex wake all releases current waiters without arming future waits") {
    const ogplay::memory::GuestAddress address{0x20000};
    ogplay::memory::AddressSpace memory;
    memory.Map({address, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus(memory);
    bus.Write32(address, 11);
    ogplay::cpu::FutexTable futex;
    std::atomic_bool first_finished{};
    auto first = ogplay::hal::StartHostThread([&] {
        first_finished = futex.Wait(bus, address, 11, 1) ==
                         ogplay::cpu::FutexWaitResult::awoken;
    });
    for (std::size_t attempt = 0;
         attempt < 100'000 && futex.WaiterCount(address) != 1; ++attempt) {
        std::this_thread::yield();
    }
    const auto first_waiting = futex.WaiterCount(address) == 1;
    if (!first_waiting) static_cast<void>(futex.WakeAll());
    REQUIRE(first_waiting);
    CHECK(futex.WakeAll() == 1);
    first->Join();
    CHECK(first_finished.load());
    CHECK(futex.WakeAll() == 0);

    std::atomic_bool future_finished{};
    auto future = ogplay::hal::StartHostThread([&] {
        future_finished = futex.Wait(bus, address, 11, 2) ==
                          ogplay::cpu::FutexWaitResult::awoken;
    });
    for (std::size_t attempt = 0;
         attempt < 100'000 && futex.WaiterCount(address) != 1; ++attempt) {
        std::this_thread::yield();
    }
    const auto future_waiting = futex.WaiterCount(address) == 1;
    if (!future_waiting) static_cast<void>(futex.WakeAll());
    REQUIRE(future_waiting);
    CHECK_FALSE(future_finished.load());
    CHECK(futex.WakeAll() == 1);
    future->Join();
    CHECK(future_finished.load());
}

TEST_CASE("futex interrupt releases current waiters and rejects future waits") {
    const ogplay::memory::GuestAddress address{0x20000};
    ogplay::memory::AddressSpace memory;
    memory.Map({address, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus(memory);
    bus.Write32(address, 13);
    ogplay::cpu::FutexTable futex;
    std::atomic<ogplay::cpu::FutexWaitResult> result{
        ogplay::cpu::FutexWaitResult::awoken};
    auto waiter = ogplay::hal::StartHostThread([&] {
        result = futex.Wait(bus, address, 13, 1);
    });
    for (std::size_t attempt = 0;
         attempt < 100'000 && futex.WaiterCount(address) != 1; ++attempt) {
        std::this_thread::yield();
    }
    const auto waiting = futex.WaiterCount(address) == 1;
    if (!waiting) static_cast<void>(futex.WakeAll());
    REQUIRE(waiting);

    CHECK(futex.InterruptAll() == 1);
    waiter->Join();
    CHECK(result.load() ==
          ogplay::cpu::FutexWaitResult::interrupted_after_wait);
    CHECK(futex.WaiterCount(address) == 0);
    CHECK(futex.Wake(address, 1) == 0);
    CHECK(futex.WakeAll() == 0);
    CHECK(futex.Wait(bus, address, 13, 2) ==
          ogplay::cpu::FutexWaitResult::interrupted);
    CHECK(futex.Wait(bus, address, 12, 2) ==
          ogplay::cpu::FutexWaitResult::value_mismatch);
}

TEST_CASE("futex diagnostic snapshot exposes a wait set and wake history") {
    const ogplay::memory::GuestAddress address{0x24000};
    ogplay::memory::AddressSpace memory;
    memory.Map({address, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus(memory);
    bus.Write32(address, 21);
    ogplay::cpu::FutexTable futex;
    auto waiter = ogplay::hal::StartHostThread([&] {
        static_cast<void>(futex.Wait(bus, address, 21, 77));
    });
    for (std::size_t attempt = 0;
         attempt < 100'000 && futex.WaiterCount(address) != 1; ++attempt) {
        std::this_thread::yield();
    }
    REQUIRE(futex.WaiterCount(address) == 1);
    const auto waiting = futex.TrySnapshot();
    REQUIRE(waiting.complete);
    REQUIRE(waiting.addresses.size() == 1U);
    REQUIRE(waiting.addresses.front().waiters.size() == 1U);
    CHECK(waiting.addresses.front().waiters.front().thread_id == 77U);
    CHECK(waiting.addresses.front().waiters.front().expected == 21U);
    CHECK_FALSE(waiting.addresses.front().waiters.front().timed);
    CHECK(futex.Wake(address, 1U) == 1U);
    waiter->Join();
    const auto woken = futex.TrySnapshot();
    REQUIRE(woken.addresses.size() == 1U);
    CHECK(woken.addresses.front().wake_count == 1U);
    CHECK(woken.addresses.front().waiters.empty());
}
