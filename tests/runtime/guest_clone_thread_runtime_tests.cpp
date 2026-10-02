#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <chrono>
#include <thread>

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

TEST_CASE("guest clone failure interrupts parent wait and preserves original exception") {
    using namespace ogplay;
    using namespace runtime;
    memory::AddressSpace memory;
    memory::CheckedMemoryBus bus(memory);
    const memory::GuestAddress code{0x10000U}, word{0x11000U};
    memory.Map({code, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::write);
    memory.Map({word, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::write);
    bus.Write32(code, 0xef000002U);
    memory.Protect({code, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::execute);
    core::CapabilityLedger ledger;
    auto dispatcher = CreateAndroidArmSyscallDispatcher(ledger);
    GuestThreadLifecycle lifecycle;
    lifecycle.Register(1);
    cpu::FutexTable futex;
    cpu::GuestThreadGroup threads{[&bus] { return std::make_unique<cpu::InterpreterCpu>(bus); }};
    std::atomic_bool notified{};
    GuestCloneThreadRuntime runtime{threads, dispatcher, lifecycle, memory, bus, futex, 2, 64,
        [&](cpu::Cpu&, const cpu::RunResult&) -> SupervisorCallProgress {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (futex.WaiterCount(word) == 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            throw std::runtime_error("fixture native child failure");
        }, {}, [&] { notified = true; }};
    A32SyscallFrame clone;
    clone.number = 120;
    clone.thread_id = 1;
    clone.arguments[0] = kLinuxCloneVm | kLinuxCloneFs | kLinuxCloneFiles | kLinuxCloneSighand |
                         kLinuxCloneThread | kLinuxCloneSysvsem;
    clone.arguments[1] = 0x12000;
    clone.cpu_state.emplace();
    clone.cpu_state->SetThreadId(1);
    clone.cpu_state->SetRegister(cpu::CoreRegister::pc, code.Value());
    REQUIRE(dispatcher.Dispatch(clone) == 2);
    CHECK(futex.Wait(bus, word, 0, 1, std::chrono::seconds(3)) == cpu::FutexWaitResult::interrupted_after_wait);
    CHECK_THROWS_WITH(runtime.RethrowFailure(), "fixture native child failure");
    CHECK_THROWS_WITH(static_cast<void>(runtime.Join(2)), "fixture native child failure");
    CHECK(notified.load());
    CHECK(lifecycle.State(2).status == GuestThreadStatus::exited);
    CHECK(lifecycle.State(1).status == GuestThreadStatus::exit_requested);
    CHECK(threads.ActiveCount() == 0);
}

namespace {
class CloneStartupFailureCpu final : public ogplay::cpu::Cpu {
public:
    ogplay::cpu::RunResult Run(std::uint64_t) override { throw std::logic_error("unexpected Run"); }
    ogplay::cpu::A32State GetState() const override { return {}; }
    void SetState(const ogplay::cpu::A32State&) override { throw std::runtime_error("initial SetState failed"); }
    void RequestHalt() noexcept override {}
};
class CloneFinalStateFailureCpu final : public ogplay::cpu::Cpu {
public:
    explicit CloneFinalStateFailureCpu(ogplay::runtime::GuestThreadLifecycle& life) : lifecycle(life) {}
    ogplay::cpu::RunResult Run(std::uint64_t) override {
        state.SetRegister(ogplay::cpu::CoreRegister::r7, 1); // exit(0)
        return {1, ogplay::cpu::RunStopReason::supervisor_call,
                ogplay::memory::GuestAddress{0x10000}, 0xef000000U, 0, std::nullopt};
    }
    ogplay::cpu::A32State GetState() const override {
        if (lifecycle.State(state.ThreadId()).status == ogplay::runtime::GuestThreadStatus::exited)
            throw std::runtime_error("final GetState failed");
        return state;
    }
    void SetState(const ogplay::cpu::A32State& value) override { state = value; }
    void RequestHalt() noexcept override {}
private:
    ogplay::runtime::GuestThreadLifecycle& lifecycle;
    ogplay::cpu::A32State state;
};
}

TEST_CASE("guest clone startup and final state failures reach the process failure notifier") {
    using namespace ogplay;
    using namespace runtime;
    memory::AddressSpace memory;
    memory::CheckedMemoryBus bus(memory);
    core::CapabilityLedger ledger;
    auto dispatcher = CreateAndroidArmSyscallDispatcher(ledger);
    GuestThreadLifecycle lifecycle;
    lifecycle.Register(1);
    BindAndroidThreadLifecycleSyscalls(dispatcher, lifecycle);
    cpu::FutexTable futex;
    const memory::GuestAddress clear_tid{0x20000};
    memory.Map({clear_tid, 4096}, memory::PageProtection::read | memory::PageProtection::write);
    bus.Write32(clear_tid, 2);
    unsigned failure_mode = 0;
    const char* expected = "CPU creation failed";
    SUBCASE("CPU creation throws") { failure_mode = 0; }
    SUBCASE("CPU factory returns null") { failure_mode = 1; expected = "CPU factory returned null"; }
    SUBCASE("initial SetState throws") { failure_mode = 2; expected = "initial SetState failed"; }
    SUBCASE("final GetState throws after guest exit") { failure_mode = 3; expected = "final GetState failed"; }
    cpu::GuestThreadGroup threads{[&]() -> std::unique_ptr<cpu::Cpu> {
        if (failure_mode == 0) throw std::runtime_error("CPU creation failed");
        if (failure_mode == 1) return {};
        if (failure_mode == 2) return std::make_unique<CloneStartupFailureCpu>();
        return std::make_unique<CloneFinalStateFailureCpu>(lifecycle);
    }};
    std::atomic_bool notified{};
    GuestCloneThreadRuntime runtime{threads, dispatcher, lifecycle, memory, bus, futex,
                                    2, 64, {}, {}, [&] { notified = true; throw std::runtime_error("secondary notifier failure"); }};
    A32SyscallFrame clone;
    clone.number = 120;
    clone.thread_id = 1;
    clone.arguments[0] = kLinuxCloneVm | kLinuxCloneFs | kLinuxCloneFiles |
                         kLinuxCloneSighand | kLinuxCloneThread | kLinuxCloneSysvsem | kLinuxCloneChildCleartid;
    clone.arguments[1] = 0x12000;
    clone.arguments[4] = clear_tid.Value();
    clone.cpu_state.emplace();
    clone.cpu_state->SetThreadId(1);
    REQUIRE(dispatcher.Dispatch(clone) == 2);
    CHECK_THROWS_WITH(static_cast<void>(runtime.Join(2)), expected);
    CHECK(notified.load());
    CHECK(bus.Read32(clear_tid) == 0);
    CHECK(lifecycle.State(2).status == GuestThreadStatus::exited);
    CHECK(lifecycle.State(1).status == GuestThreadStatus::exit_requested);
    CHECK_THROWS_WITH(runtime.RethrowFailure(), expected);
}

TEST_CASE("guest clone CPU faults publish a process failure with guest diagnostics") {
    using namespace ogplay;
    using namespace runtime;
    memory::AddressSpace memory;
    memory::CheckedMemoryBus bus(memory);
    const memory::GuestAddress code{0x10000U};
    memory.Map({code, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::write);
    bool bad_instruction = false;
    SUBCASE("unmapped null read") {}
    SUBCASE("unsupported instruction") { bad_instruction = true; }
    bus.Write32(code, bad_instruction ? 0xe7f000f0U : 0xe5900000U); // UDF or ldr r0,[r0]
    memory.Protect({code, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::execute);
    core::CapabilityLedger ledger;
    auto dispatcher = CreateAndroidArmSyscallDispatcher(ledger);
    GuestThreadLifecycle lifecycle;
    lifecycle.Register(1);
    cpu::FutexTable futex;
    cpu::GuestThreadGroup threads{[&bus] { return std::make_unique<cpu::InterpreterCpu>(bus); }};
    std::atomic_bool notified{};
    GuestCloneThreadRuntime runtime{threads, dispatcher, lifecycle, memory, bus, futex,
                                    2, 64, {}, {}, [&] { notified = true; }};
    A32SyscallFrame clone;
    clone.number = 120;
    clone.thread_id = 1;
    clone.arguments[0] = kLinuxCloneVm | kLinuxCloneFs | kLinuxCloneFiles |
                         kLinuxCloneSighand | kLinuxCloneThread | kLinuxCloneSysvsem;
    clone.arguments[1] = 0x12000;
    clone.cpu_state.emplace();
    clone.cpu_state->SetThreadId(1);
    clone.cpu_state->SetRegister(cpu::CoreRegister::pc, code.Value());
    REQUIRE(dispatcher.Dispatch(clone) == 2);
    std::string first;
    try { static_cast<void>(runtime.Join(2)); FAIL("faulting clone silently exited"); }
    catch (const A32GuestCallError& error) { first = error.what(); }
    CHECK(first.find("thread:    guest=2") != std::string::npos);
    CHECK(first.find("pc=0x00010000") != std::string::npos);
    if (!bad_instruction) {
        CHECK(first.find("reason=memory_fault(4)") != std::string::npos);
        CHECK(first.find("address=0x00000000 access=read(0)") != std::string::npos);
    }
    CHECK_THROWS_WITH(runtime.RethrowFailure(), first.c_str());
    CHECK_THROWS_WITH(runtime.RethrowFailure(), first.c_str());
    CHECK(notified.load());
    CHECK(lifecycle.State(1).status == GuestThreadStatus::exit_requested);
    CHECK(lifecycle.State(2).status == GuestThreadStatus::exited);
}
