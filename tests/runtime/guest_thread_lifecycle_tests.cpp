#include <doctest/doctest.h>

#include <cstdint>
#include <atomic>
#include <thread>

#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/guest_thread_lifecycle.h"

TEST_CASE("guest thread lifecycle advances explicit per-thread states") {
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    lifecycle.Register(11, ogplay::memory::GuestAddress{0x72000000U});
    lifecycle.Register(12);
    lifecycle.SetThreadPointer(12,
                               ogplay::memory::GuestAddress{0x72001000U});
    lifecycle.SetClearChildTid(12,
                               ogplay::memory::GuestAddress{0x10020U});
    lifecycle.RequestExit(12, 3);
    auto state = lifecycle.State(12);
    CHECK(state.status ==
          ogplay::runtime::GuestThreadStatus::exit_requested);
    CHECK(state.thread_pointer ==
          ogplay::memory::GuestAddress{0x72001000U});
    CHECK(state.clear_child_tid ==
          ogplay::memory::GuestAddress{0x10020U});
    CHECK(state.exit_code == 3);
    CHECK(lifecycle.CompleteExit(12).status ==
          ogplay::runtime::GuestThreadStatus::exited);
    CHECK(lifecycle.Reap(12).thread_id == 12);
    CHECK_THROWS_AS(static_cast<void>(lifecycle.State(12)),
                    ogplay::runtime::GuestThreadLifecycleError);
}

TEST_CASE("exit group marks every live guest thread with one result") {
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    lifecycle.Register(21);
    lifecycle.Register(22);
    lifecycle.Register(23);
    lifecycle.RequestExit(23, 1);
    lifecycle.RequestExitGroup(21, 9);
    const auto states = lifecycle.States();
    REQUIRE(states.size() == 3);
    for (const auto& state : states) {
        CHECK(state.status ==
              ogplay::runtime::GuestThreadStatus::exit_requested);
        CHECK(state.exit_code == 9);
    }
}

TEST_CASE("guest thread lifecycle rejects invalid transitions") {
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    CHECK_THROWS_AS(lifecycle.Register(0),
                    ogplay::runtime::GuestThreadLifecycleError);
    lifecycle.Register(31);
    CHECK_THROWS_AS(lifecycle.Register(31),
                    ogplay::runtime::GuestThreadLifecycleError);
    CHECK_THROWS_AS(static_cast<void>(lifecycle.CompleteExit(31)),
                    ogplay::runtime::GuestThreadLifecycleError);
    CHECK_THROWS_AS(static_cast<void>(lifecycle.Reap(31)),
                    ogplay::runtime::GuestThreadLifecycleError);
    CHECK_THROWS_AS(lifecycle.RequestExit(99, 0),
                    ogplay::runtime::GuestThreadLifecycleError);
}

TEST_CASE("guest exit clears child tid and wakes one futex waiter") {
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress address{0x10000U};
    memory.Map({address, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus(memory);
    bus.Write32(address, 52);
    ogplay::cpu::FutexTable futex;
    std::atomic_bool awoken{false};
    std::thread waiter([&] {
        awoken = futex.Wait(bus, address, 52, 99) ==
                 ogplay::cpu::FutexWaitResult::awoken;
    });
    while (futex.WaiterCount(address) == 0) std::this_thread::yield();

    ogplay::runtime::GuestThreadLifecycle lifecycle;
    lifecycle.Register(52);
    lifecycle.SetClearChildTid(52, address);
    lifecycle.RequestExit(52, 7);
    const auto completed = lifecycle.CompleteExit(52, bus, futex);
    waiter.join();
    CHECK(completed.cleanup ==
          ogplay::runtime::GuestThreadCleanupStatus::cleared);
    CHECK(completed.futex_wake_count == 1);
    CHECK(bus.Read32(address) == 0);
    CHECK(awoken.load());
    CHECK(completed.state.status ==
          ogplay::runtime::GuestThreadStatus::exited);
}

TEST_CASE("guest exit reports cleanup faults without reverting exit state") {
    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus(memory);
    ogplay::cpu::FutexTable futex;
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    lifecycle.Register(61);
    lifecycle.SetClearChildTid(61,
                               ogplay::memory::GuestAddress{0x20000U});
    lifecycle.RequestExit(61, 0);
    CHECK(lifecycle.CompleteExit(61, bus, futex).cleanup ==
          ogplay::runtime::GuestThreadCleanupStatus::memory_fault);
    CHECK(lifecycle.State(61).status ==
          ogplay::runtime::GuestThreadStatus::exited);

    lifecycle.Register(62);
    lifecycle.SetClearChildTid(62,
                               ogplay::memory::GuestAddress{0x20001U});
    lifecycle.RequestExit(62, 0);
    CHECK(lifecycle.CompleteExit(62, bus, futex).cleanup ==
          ogplay::runtime::GuestThreadCleanupStatus::invalid_address);
}
