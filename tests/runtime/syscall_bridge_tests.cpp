#include <doctest/doctest.h>

#include <cstdint>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/cpu/dynarmic.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/syscall/syscall_bridge.h"

TEST_CASE("ARM cacheflush publishes patched ARM and Thumb code to all process CPUs") {
    using namespace ogplay;
    for (const bool thumb : {false, true}) {
        memory::AddressSpace memory;
        const memory::GuestAddress code{0x10000U};
        memory.Map({code, memory.PageSize()}, memory::PageProtection::read |
            memory::PageProtection::write | memory::PageProtection::execute);
        memory::CheckedMemoryBus bus(memory);
        const auto patch = [&](const std::uint32_t value) {
            if (thumb) bus.Write16(code, static_cast<std::uint16_t>(0x2000U | value));
            else bus.Write32(code, 0xe3a00000U | value); // mov r0, #value
        };
        patch(1);
        if (thumb) bus.Write16(code.Add(2), 0xdf01U);
        else bus.Write32(code.Add(4), 0xef000001U);
        bus.Write32(code.Add(64), 0xef000000U); // cacheflush SVC, ARM
        auto context = std::make_shared<cpu::DynarmicExecutionContext>(3);
        cpu::DynarmicCpu first(bus, context), second(bus, context);
        const auto execute = [&](cpu::Cpu& processor) {
            cpu::A32State state;
            state.SetRegister(cpu::CoreRegister::pc, code.Value());
            state.SetState(thumb ? cpu::ExecutionState::thumb : cpu::ExecutionState::a32);
            processor.SetState(state);
            CHECK(processor.Run(16).reason == cpu::RunStopReason::supervisor_call);
            return processor.GetState().Register(cpu::CoreRegister::r0);
        };
        CHECK(execute(first) == 1);
        CHECK(execute(second) == 1);
        { cpu::DynarmicCpu retired(bus, context); CHECK(execute(retired) == 1); }
        core::CapabilityLedger ledger;
        auto dispatcher = runtime::CreateAndroidArmSyscallDispatcher(ledger);
        runtime::BindAndroidArmPrivateSyscalls(dispatcher, memory,
            [](std::uint64_t, memory::GuestAddress) { return true; });
        const auto flush = [&](std::uint32_t start, std::uint32_t end, std::uint32_t flags) {
            cpu::A32State state;
            state.SetRegister(cpu::CoreRegister::pc, code.Add(64).Value());
            state.SetRegister(cpu::CoreRegister::r7, 0xf0002U);
            state.SetRegister(cpu::CoreRegister::r0, start);
            state.SetRegister(cpu::CoreRegister::r1, end);
            state.SetRegister(cpu::CoreRegister::r2, flags);
            first.SetState(state);
            const auto stop = first.Run(16);
            const auto result = runtime::DispatchAndroidArmSupervisorCall(first, stop, dispatcher);
            REQUIRE(result.has_value());
            return result->return_value;
        };
        patch(2);
        CHECK(flush(0x10000, 0x10004, 1) == -22);
        CHECK(flush(0x20000, 0x20004, 0) == -14);
        CHECK(flush(0x10004, 0x10000, 0) == -22);
        CHECK(flush(0x10000, 0x10000, 0) == 0);
        // Empty/invalid requests and disjoint ranges must not flush the cache.
        CHECK(flush(0x10800, 0x10804, 0) == 0);
        CHECK(execute(second) == 1);
        CHECK(flush(0x10000, 0x10004, 0) == 0);
        CHECK(execute(first) == 2);
        CHECK(execute(second) == 2);
        cpu::DynarmicCpu replacement(bus, context);
        CHECK(execute(replacement) == 2);
        patch(3);
        CHECK(flush(0x10000, 0x10004, 0) == 0);
        CHECK(execute(replacement) == 3);
        CHECK(execute(second) == 3);
    }
}

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
