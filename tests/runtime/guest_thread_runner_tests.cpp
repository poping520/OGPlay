#include <doctest/doctest.h>

#include <cstdint>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/guest_thread_runner.h"

namespace {

struct RunnerFixture final {
    RunnerFixture() : bus(memory), cpu(bus) {
        memory.Map({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
    }

    void Start(const std::uint64_t thread_id) {
        memory.Protect({code, memory.PageSize()},
                       ogplay::memory::PageProtection::read |
                           ogplay::memory::PageProtection::execute);
        ogplay::cpu::A32State state;
        state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
        state.SetThreadId(thread_id);
        cpu.SetState(state);
        lifecycle.Register(thread_id);
    }

    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus;
    ogplay::cpu::InterpreterCpu cpu;
    ogplay::cpu::FutexTable futex;
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::A32SyscallDispatcher dispatcher{
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger)};
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    const ogplay::memory::GuestAddress code{0x10000U};
};

}  // namespace

TEST_CASE("guest thread runner consumes SVC exit through lifecycle") {
    RunnerFixture fixture;
    fixture.bus.Write32(fixture.code, 0xe3a07001U);         // mov r7, #1
    fixture.bus.Write32(fixture.code.Add(4), 0xe3a00007U);  // mov r0, #7
    fixture.bus.Write32(fixture.code.Add(8), 0xef000000U);  // svc #0
    fixture.Start(77);
    ogplay::runtime::BindAndroidThreadLifecycleSyscalls(
        fixture.dispatcher, fixture.lifecycle);
    const auto outcome = ogplay::runtime::RunAndroidArmGuestThread(
        fixture.cpu, fixture.dispatcher, fixture.lifecycle, fixture.bus,
        fixture.futex, 16);
    CHECK(outcome.reason == ogplay::runtime::GuestThreadRunStop::guest_exit);
    REQUIRE(outcome.exit.has_value());
    CHECK(outcome.exit->state.exit_code == 7);
    CHECK(outcome.exit->state.status ==
          ogplay::runtime::GuestThreadStatus::exited);
    CHECK(outcome.ticks_consumed == 3);
}

TEST_CASE("guest thread runner preserves unhandled traps and budgets") {
    SUBCASE("non-Linux SVC") {
        RunnerFixture fixture;
        fixture.bus.Write32(fixture.code, 0xef000001U);
        fixture.Start(81);
        const auto outcome = ogplay::runtime::RunAndroidArmGuestThread(
            fixture.cpu, fixture.dispatcher, fixture.lifecycle, fixture.bus,
            fixture.futex, 4);
        CHECK(outcome.reason ==
              ogplay::runtime::GuestThreadRunStop::unhandled_supervisor_call);
    }
    SUBCASE("budget") {
        RunnerFixture fixture;
        fixture.bus.Write32(fixture.code, 0xeafffffeU);  // b .
        fixture.Start(82);
        const auto outcome = ogplay::runtime::RunAndroidArmGuestThread(
            fixture.cpu, fixture.dispatcher, fixture.lifecycle, fixture.bus,
            fixture.futex, 5);
        CHECK(outcome.reason ==
              ogplay::runtime::GuestThreadRunStop::budget_exhausted);
        CHECK(outcome.ticks_consumed == 5);
    }
}

TEST_CASE("guest thread runner commits set_tls to the active CPU") {
    RunnerFixture fixture;
    fixture.bus.Write32(fixture.code, 0xef000000U);         // svc #0: set_tls
    fixture.bus.Write32(fixture.code.Add(4), 0xef000001U);  // svc #1: stop
    fixture.Start(83);
    auto state = fixture.cpu.GetState();
    state.SetRegister(ogplay::cpu::CoreRegister::r7, 0x0f0005U);
    state.SetRegister(ogplay::cpu::CoreRegister::r0, 0x72000000U);
    fixture.cpu.SetState(state);
    ogplay::runtime::BindAndroidArmPrivateSyscalls(
        fixture.dispatcher,
        [&fixture](const std::uint64_t thread_id,
                   const ogplay::memory::GuestAddress pointer) {
            fixture.lifecycle.SetThreadPointer(thread_id, pointer);
            return true;
        });

    const auto outcome = ogplay::runtime::RunAndroidArmGuestThread(
        fixture.cpu, fixture.dispatcher, fixture.lifecycle, fixture.bus,
        fixture.futex, 8);
    CHECK(outcome.reason ==
          ogplay::runtime::GuestThreadRunStop::unhandled_supervisor_call);
    CHECK(fixture.cpu.GetState().ThreadPointer() ==
          ogplay::memory::GuestAddress{0x72000000U});
}
