#include <doctest/doctest.h>

#include <cstdint>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/syscall/syscall_bridge.h"

TEST_CASE("A32 SVC zero dispatches Linux register ABI and writes r0") {
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress code{0x10000U};
    memory.Map({code, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus(memory);
    bus.Write32(code, 0xe3a07014U);         // mov r7, #20 (getpid)
    bus.Write32(code.Add(4), 0xef000000U);  // svc #0
    bus.Write32(code.Add(8), 0xef000001U);  // non-Linux svc #1
    memory.Protect({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::execute);
    ogplay::cpu::InterpreterCpu cpu(bus);
    ogplay::cpu::A32State state;
    state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::lr, 0x12345678U);
    state.SetThreadId(77);
    state.SetThreadPointer(ogplay::memory::GuestAddress{0x72000000U});
    cpu.SetState(state);
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher = ogplay::runtime::CreateAndroidArmSyscallDispatcher(
        ledger, {.process_id = 4242});

    const auto stop = cpu.Run(4);
    const auto dispatched =
        ogplay::runtime::DispatchAndroidArmSupervisorCall(
            cpu, stop, dispatcher);
    REQUIRE(dispatched.has_value());
    CHECK(dispatched->number == 20);
    CHECK(dispatched->return_value == 4242);
    CHECK(dispatched->cpu_state.Register(ogplay::cpu::CoreRegister::r0) ==
          4242U);
    CHECK(dispatched->cpu_state.Register(ogplay::cpu::CoreRegister::pc) ==
          code.Value() + 8U);
    CHECK(dispatched->cpu_state.ThreadId() == 77);
    CHECK(dispatched->cpu_state.ThreadPointer() ==
          ogplay::memory::GuestAddress{0x72000000U});

    const auto other = cpu.Run(1);
    CHECK_FALSE(ogplay::runtime::DispatchAndroidArmSupervisorCall(
                    cpu, other, dispatcher)
                    .has_value());
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::DispatchAndroidArmSupervisorCall(
            cpu,
            {0, ogplay::cpu::RunStopReason::budget_exhausted, code, 0, 0,
             std::nullopt},
            dispatcher)),
        ogplay::runtime::SyscallError);
}
