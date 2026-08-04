#include <doctest/doctest.h>

#include <cstdint>

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
