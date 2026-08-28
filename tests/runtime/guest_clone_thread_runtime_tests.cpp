#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/runtime/execution/guest_clone_thread_runtime.h"

TEST_CASE("ARM clone starts a real host thread at the child return path") {
    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus(memory);
    const ogplay::memory::GuestAddress code{0x10000U};
    const ogplay::memory::GuestAddress tid{0x11000U};
    memory.Map({code, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    memory.Map({tid, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    bus.Write32(code, 0xef000000U);          // svc #0: clone
    bus.Write32(code.Add(4), 0xe3500000U);   // cmp r0, #0
    bus.Write32(code.Add(8), 0x1a000003U);   // bne parent_stop
    bus.Write32(code.Add(12), 0xef000002U);  // svc #2: child HLE
    bus.Write32(code.Add(16), 0xe3a07001U);  // mov r7, #1
    bus.Write32(code.Add(20), 0xe3a00007U);  // mov r0, #7
    bus.Write32(code.Add(24), 0xef000000U);  // svc #0: exit
    bus.Write32(code.Add(28), 0xef000001U);  // parent_stop: svc #1
    memory.Protect({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::execute);

    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    lifecycle.Register(41);
    ogplay::cpu::FutexTable futex;
    ogplay::cpu::GuestThreadGroup threads{
        [&bus] { return std::make_unique<ogplay::cpu::InterpreterCpu>(bus); }};
    ogplay::runtime::BindAndroidThreadLifecycleSyscalls(dispatcher, lifecycle);
    std::atomic_uint32_t hle_calls{};
    ogplay::runtime::GuestCloneThreadRuntime clone_runtime{
        threads, dispatcher, lifecycle, memory, bus, futex, 100, 16,
        [&hle_calls](ogplay::cpu::Cpu&,
                     const ogplay::cpu::RunResult& stopped) {
            if (stopped.immediate != 2) {
                return ogplay::runtime::SupervisorCallProgress::not_handled;
            }
            ++hle_calls;
            return ogplay::runtime::SupervisorCallProgress::handled_idle;
        }};

    ogplay::cpu::InterpreterCpu parent(bus);
    ogplay::cpu::A32State state;
    state.SetThreadId(41);
    state.SetThreadPointer(ogplay::memory::GuestAddress{0x70000000U});
    state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r7, 120);
    state.SetRegister(
        ogplay::cpu::CoreRegister::r0,
        ogplay::runtime::kLinuxCloneVm | ogplay::runtime::kLinuxCloneFs |
            ogplay::runtime::kLinuxCloneFiles |
            ogplay::runtime::kLinuxCloneSighand |
            ogplay::runtime::kLinuxCloneThread |
            ogplay::runtime::kLinuxCloneSysvsem |
            ogplay::runtime::kLinuxCloneParentSettid |
            ogplay::runtime::kLinuxCloneChildCleartid);
    state.SetRegister(ogplay::cpu::CoreRegister::r1, 0x12000U);
    state.SetRegister(ogplay::cpu::CoreRegister::r2, tid.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r3, 0x72000000U);
    state.SetRegister(ogplay::cpu::CoreRegister::r4, tid.Value());
    parent.SetState(state);
    const auto parent_run = ogplay::runtime::RunAndroidArmGuestThread(
        parent, dispatcher, lifecycle, bus, futex, 16);
    CHECK(parent_run.reason ==
          ogplay::runtime::GuestThreadRunStop::unhandled_supervisor_call);
    CHECK(parent.GetState().Register(ogplay::cpu::CoreRegister::r0) == 100);

    const auto child = clone_runtime.Join(100);
    CHECK(child.run.reason ==
          ogplay::runtime::GuestThreadRunStop::guest_exit);
    REQUIRE(child.run.exit.has_value());
    CHECK(child.run.exit->state.exit_code == 7);
    CHECK(child.thread.cpu_state.ThreadId() == 100);
    CHECK(child.thread.cpu_state.ThreadPointer() ==
          ogplay::memory::GuestAddress{0x70000000U});
    CHECK(bus.Read32(tid) == 0);
    CHECK(hle_calls.load() == 1);
}

TEST_CASE("guest clone observes an external exit request between slices") {
    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus(memory);
    const ogplay::memory::GuestAddress code{0x10000U};
    memory.Map({code, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    bus.Write32(code, 0xef000000U);          // svc #0: clone
    bus.Write32(code.Add(4), 0xe3500000U);   // cmp r0, #0
    bus.Write32(code.Add(8), 0x1a000000U);   // bne parent_stop
    bus.Write32(code.Add(12), 0xeafffffeU);  // child: b .
    bus.Write32(code.Add(16), 0xef000001U);  // parent_stop: svc #1
    memory.Protect({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::execute);

    ogplay::core::CapabilityLedger ledger;
    auto dispatcher = ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    lifecycle.Register(1);
    ogplay::cpu::FutexTable futex;
    ogplay::cpu::GuestThreadGroup threads{
        [&bus] { return std::make_unique<ogplay::cpu::InterpreterCpu>(bus); }};
    ogplay::runtime::GuestCloneThreadRuntime clone_runtime{
        threads, dispatcher, lifecycle, memory, bus, futex, 2, 64};

    ogplay::cpu::InterpreterCpu parent(bus);
    ogplay::cpu::A32State state;
    state.SetThreadId(1);
    state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r7, 120);
    state.SetRegister(
        ogplay::cpu::CoreRegister::r0,
        ogplay::runtime::kLinuxCloneVm | ogplay::runtime::kLinuxCloneFs |
            ogplay::runtime::kLinuxCloneFiles |
            ogplay::runtime::kLinuxCloneSighand |
            ogplay::runtime::kLinuxCloneThread |
            ogplay::runtime::kLinuxCloneSysvsem);
    state.SetRegister(ogplay::cpu::CoreRegister::r1, 0x12000U);
    parent.SetState(state);
    const auto parent_run = ogplay::runtime::RunAndroidArmGuestThread(
        parent, dispatcher, lifecycle, bus, futex, 64);
    CHECK(parent_run.reason ==
          ogplay::runtime::GuestThreadRunStop::unhandled_supervisor_call);
    lifecycle.RequestExit(2, 0);
    const auto joined = clone_runtime.Join(2);
    CHECK(joined.run.reason == ogplay::runtime::GuestThreadRunStop::guest_exit);
    REQUIRE(joined.run.exit.has_value());
    CHECK(joined.run.exit->state.status ==
          ogplay::runtime::GuestThreadStatus::exited);
    CHECK(threads.ActiveCount() == 0U);
}
