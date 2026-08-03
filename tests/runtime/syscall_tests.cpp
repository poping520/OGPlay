#include <doctest/doctest.h>

#include <cstdint>

#include "ogplay/runtime/syscall.h"

TEST_CASE("Android ARM syscall baseline exposes identity and coverage") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher = ogplay::runtime::CreateAndroidArmSyscallDispatcher(
        ledger, {.process_id = 42, .user_id = 10001, .group_id = 10002});
    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 20;
    CHECK(dispatcher.Dispatch(frame) == 42);
    frame.number = 199;
    CHECK(dispatcher.Dispatch(frame) == 10001);
    frame.number = 200;
    CHECK(dispatcher.Dispatch(frame) == 10002);
    frame.number = 224;
    frame.thread_id = 77;
    CHECK(dispatcher.Dispatch(frame) == 77);
    const auto coverage = dispatcher.Coverage();
    CHECK(coverage.declared >= 75);
    CHECK(coverage.implemented == 6);
    CHECK(coverage.declared_by_group.at(
              ogplay::runtime::SyscallGroup::file) > 20);
}

TEST_CASE("declared and unknown syscalls return ENOSYS and are observable") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 3;
    frame.link_register = 0x1234;
    CHECK(dispatcher.Dispatch(frame) == -ogplay::runtime::kLinuxEnosys);
    frame.number = 999;
    frame.link_register = 0x5678;
    CHECK(dispatcher.Dispatch(frame) == -ogplay::runtime::kLinuxEnosys);
    const auto hits = ledger.Unimplemented();
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].id == "syscall.arm.999");
    CHECK(hits[0].first_lr == 0x5678);
    CHECK(hits[1].id == "syscall.read");
    CHECK(hits[1].first_lr == 0x1234);
}

TEST_CASE("syscall declarations reject duplicate numbers and empty handlers") {
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::A32SyscallDispatcher dispatcher{ledger};
    dispatcher.Declare(1, "first", ogplay::runtime::SyscallGroup::process);
    CHECK_THROWS_AS(
        dispatcher.Declare(1, "second", ogplay::runtime::SyscallGroup::process),
        ogplay::runtime::SyscallError);
    CHECK_THROWS_AS(
        dispatcher.Register(2, "empty", ogplay::runtime::SyscallGroup::process,
                            {}),
        ogplay::runtime::SyscallError);
}
