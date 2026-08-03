#include <doctest/doctest.h>

#include <cstdint>
#include <array>
#include <cstddef>

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

TEST_CASE("Android time syscalls use the unified clock and checked guest memory") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::hal::FixedStepClock clock{100, 1000};
    clock.AdvanceFrames(12);
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestRange page{
        ogplay::memory::GuestAddress{0x10000}, memory.PageSize()};
    memory.Map(page, ogplay::memory::PageProtection::read |
                         ogplay::memory::PageProtection::write);
    ogplay::runtime::BindAndroidTimeSyscalls(dispatcher, clock, memory);

    const auto read32 = [&memory](const std::uint32_t address) {
        std::array<std::byte, 4> bytes{};
        memory.Read(ogplay::memory::GuestAddress{address}, bytes);
        std::uint32_t value{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            value |= static_cast<std::uint32_t>(
                         std::to_integer<std::uint8_t>(bytes[index]))
                     << static_cast<unsigned>(index * 8U);
        }
        return value;
    };

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 263;
    frame.arguments[0] = 1;
    frame.arguments[1] = 0x10000;
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(read32(0x10000) == 1);
    CHECK(read32(0x10004) == 200000000);

    frame.number = 78;
    frame.arguments[0] = 0x10010;
    frame.arguments[1] = 0x10020;
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(read32(0x10010) == 1);
    CHECK(read32(0x10014) == 200000);
    CHECK(read32(0x10020) == 0);

    frame.number = 263;
    frame.arguments[0] = 99;
    CHECK(dispatcher.Dispatch(frame) == -22);
    frame.arguments[0] = 1;
    frame.arguments[1] = 0x20000;
    CHECK(dispatcher.Dispatch(frame) == -14);
}
