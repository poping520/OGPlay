#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <future>
#include <thread>
#include "ogplay/cpu/interpreter.h"
#include "ogplay/cpu/dynarmic.h"
#include "ogplay/runtime/execution/guest_thread_runner.h"
#include <cstddef>
#include <cstdint>

#include "ogplay/runtime/syscall/syscall.h"

namespace {

void Write32(ogplay::memory::AddressSpace& memory,
             const ogplay::memory::GuestAddress address,
             const std::uint32_t value) {
    std::array<std::byte, 4> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
    memory.Write(address, bytes);
}

[[nodiscard]] std::uint32_t Read32(
    ogplay::memory::AddressSpace& memory,
    const ogplay::memory::GuestAddress address) {
    std::array<std::byte, 4> bytes{};
    memory.Read(address, bytes);
    std::uint32_t result{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[index]))
                  << static_cast<unsigned>(index * 8U);
    }
    return result;
}

}  // namespace

TEST_CASE("ARM signal masks are checked and isolated per guest thread") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress page{0x10000U};
    memory.Map({page, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::runtime::BindAndroidSignalSyscalls(dispatcher, memory);
    Write32(memory, page, 0xffffffffU);

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 126;
    frame.thread_id = 41;
    frame.arguments[0] = 2;
    frame.arguments[1] = page.Value();
    frame.arguments[2] = page.Add(8).Value();
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(Read32(memory, page.Add(8)) == 0);
    frame.arguments[1] = 0;
    frame.arguments[2] = page.Add(12).Value();
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK((Read32(memory, page.Add(12)) & (1U << 8U)) == 0);
    CHECK((Read32(memory, page.Add(12)) & (1U << 18U)) == 0);

    frame.thread_id = 42;
    frame.arguments[2] = page.Add(16).Value();
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(Read32(memory, page.Add(16)) == 0);
    frame.thread_id = 0;
    CHECK(dispatcher.Dispatch(frame) == -3);
    frame.thread_id = 41;
    frame.arguments[1] = 0x20000U;
    CHECK(dispatcher.Dispatch(frame) == -14);

    frame.number = 175;
    frame.arguments[1] = 0;
    frame.arguments[2] = page.Add(24).Value();
    frame.arguments[3] = 4;
    CHECK(dispatcher.Dispatch(frame) == -22);
    frame.arguments[3] = 8;
    CHECK(dispatcher.Dispatch(frame) == 0);
}

TEST_CASE("ARM sigaltstack stores enabled and disabled thread state") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress page{0x10000U};
    memory.Map({page, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::runtime::BindAndroidSignalSyscalls(dispatcher, memory);
    Write32(memory, page, 0x60000000U);
    Write32(memory, page.Add(4), 0);
    Write32(memory, page.Add(8), 8192);

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 186;
    frame.thread_id = 51;
    frame.arguments[0] = page.Value();
    CHECK(dispatcher.Dispatch(frame) == 0);
    frame.arguments[0] = 0;
    frame.arguments[1] = page.Add(16).Value();
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(Read32(memory, page.Add(16)) == 0x60000000U);
    CHECK(Read32(memory, page.Add(20)) == 0);
    CHECK(Read32(memory, page.Add(24)) == 8192);

    Write32(memory, page.Add(4), 2);
    frame.arguments[0] = page.Value();
    frame.arguments[1] = 0;
    CHECK(dispatcher.Dispatch(frame) == 0);
    frame.arguments[0] = 0;
    frame.arguments[1] = page.Add(32).Value();
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(Read32(memory, page.Add(36)) == 2);
    frame.arguments[0] = 0x20000U;
    CHECK(dispatcher.Dispatch(frame) == -14);
}

TEST_CASE("ARM tgkill SIGABRT records signal termination and exits process") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::memory::AddressSpace memory;
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    lifecycle.Register(71U);
    lifecycle.RegisterChild(71U, 72U, ogplay::memory::GuestAddress{0},
                            ogplay::memory::GuestAddress{0});
    ogplay::runtime::BindAndroidSignalSyscalls(dispatcher, memory, &lifecycle);

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 268U;
    frame.thread_id = 71U;
    frame.arguments = {1000U, 71U, 6U};
    frame.program_counter = 0x12345678U;
    frame.link_register = 0x87654321U;
    CHECK(dispatcher.Dispatch(frame) == 0);
    for (const auto thread : {71U, 72U}) {
        const auto state = lifecycle.State(thread);
        CHECK(state.status == ogplay::runtime::GuestThreadStatus::exit_requested);
        CHECK(state.exit_code == 134);
        CHECK(state.exit_request.origin ==
              ogplay::runtime::GuestThreadExitOrigin::signal_termination);
        CHECK(state.exit_request.signal_number == 6U);
        CHECK(state.exit_request.target_thread_id == 71U);
        CHECK(state.exit_request.program_counter == 0x12345678U);
    }
}

namespace {
using namespace ogplay;
using memory::GuestAddress;
using cpu::CoreRegister;

template <typename Cpu> struct SignalFixture {
    memory::AddressSpace memory;
    memory::CheckedMemoryBus bus{memory};
    Cpu cpu{bus};
    core::CapabilityLedger ledger;
    runtime::GuestThreadLifecycle lifecycle;
    cpu::FutexTable futex;
    runtime::A32SyscallDispatcher dispatcher{runtime::CreateAndroidArmSyscallDispatcher(ledger)};
    SignalFixture() {
        memory.Map({GuestAddress{0x10000}, 0x1000}, memory::PageProtection::read | memory::PageProtection::write | memory::PageProtection::execute);
        memory.Map({GuestAddress{0x20000}, 0x4000}, memory::PageProtection::read | memory::PageProtection::write);
        lifecycle.Register(1); lifecycle.Register(2);
        runtime::BindAndroidThreadSyscalls(dispatcher, futex, bus);
        runtime::BindAndroidSignalSyscalls(dispatcher, memory, &lifecycle);
        cpu::A32State state;
        state.SetThreadId(2);
        state.SetRegister(CoreRegister::sp, 0x24000);
        state.SetRegister(CoreRegister::pc, 0x10000);
        cpu.SetState(state);
    }
    std::int32_t Call(std::uint32_t number, std::array<std::uint32_t, 7> args, std::uint64_t id = 2) {
        runtime::A32SyscallFrame frame;
        frame.number = number; frame.arguments = args; frame.thread_id = id;
        return dispatcher.Dispatch(frame);
    }
    void Action(std::uint32_t signal, std::uint32_t address, std::uint32_t flags = 0, std::uint32_t mask = 0) {
        memory.Write32(GuestAddress{0x20000}, address);
        memory.Write32(GuestAddress{0x20004}, mask);
        memory.Write32(GuestAddress{0x20008}, flags);
        memory.Write32(GuestAddress{0x2000c}, 0);
        REQUIRE(Call(67, {signal, 0x20000}) == 0);
    }
    std::int32_t Send(std::uint32_t signal) { return Call(268, {1000, 2, signal}, 1); }
    void Code(std::uint32_t at, std::initializer_list<std::uint32_t> words) {
        for (const auto word : words) { memory.Write32(GuestAddress{at}, word); at += 4; }
    }
};

template <typename Predicate> bool Await(Predicate predicate) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= limit) return false;
        std::this_thread::yield();
    }
    return true;
}
}

TEST_CASE("ARM signal action ABI validates inputs and preserves old disposition") {
    SignalFixture<cpu::InterpreterCpu> f;
    f.Action(30, 0x10100, 0x10000000, 0x80000000);
    REQUIRE(f.Call(174, {30, 0, 0x20040, 8}) == 0);
    CHECK(f.memory.Read32(GuestAddress{0x20040}) == 0x10100);
    CHECK(f.memory.Read32(GuestAddress{0x20044}) == 0x10000000);
    CHECK(f.memory.Read64(GuestAddress{0x2004c}) == 0x80000000);
    CHECK(f.Call(174, {30, 0, 0x20040, 4}) == -22);
    CHECK(f.Call(67, {9, 0x20000}) == -22);
    CHECK(f.Call(67, {19, 0x20000}) == -22);
    CHECK(f.Call(67, {30, 0x30000}) == -14);
    CHECK(f.Call(67, {30, 0x20000, 0x30000}) == -14);
    CHECK(f.Call(268, {999, 2, 30}) == -3);
    CHECK(f.Call(268, {1000, 999, 30}) == -3);
    CHECK(f.Send(32) == -38);
    f.lifecycle.RequestExit(2, 0);
    CHECK(f.Send(0) == -3);
}

TEST_CASE_TEMPLATE("ARM signal handlers restore core Thumb VFP and mask state", Cpu,
                   cpu::InterpreterCpu, cpu::DynarmicCpu) {
    for (const auto flags : {0U, 4U}) {
        SignalFixture<Cpu> f;
        f.Code(0x10100, {0xe3a00063, 0xe12fff1e}); // mov r0,#99; bx lr
        f.Action(30, 0x10100, flags);
        auto initial = f.cpu.GetState();
        initial.SetState(cpu::ExecutionState::thumb);
        initial.SetRegister(CoreRegister::r0, 42);
        initial.SetRegister(CoreRegister::lr, 0x10801);
        initial.SetCpsr(initial.Cpsr() | 0xa0000000);
        initial.SetFpscr(0x00c00000);
        initial.SetFpexc(0x40000000);
        for (std::uint8_t i = 0; i < 64; ++i) initial.SetExtendedRegister(i, 0x12340000U + i);
        f.cpu.SetState(initial);
        REQUIRE(f.Send(30) == 0);
        f.dispatcher.signal_binding->runtime->Deliver(f.cpu);
        CHECK(f.cpu.GetState().Register(CoreRegister::r0) == 30);
        if (flags) {
            const GuestAddress info{f.cpu.GetState().Register(CoreRegister::r1)};
            CHECK(f.memory.Read32(info) == 30);
            CHECK(f.memory.Read32(info.Add(8)) == 0xfffffffaU);
            const GuestAddress context{f.cpu.GetState().Register(CoreRegister::r2)};
            CHECK(f.memory.Read32(context.Add(32)) == 42);
        }
        const auto stop = f.cpu.Run(30);
        REQUIRE(stop.reason == cpu::RunStopReason::supervisor_call);
        CHECK(f.cpu.GetState().Register(CoreRegister::r7) == (flags ? 173 : 119));
        REQUIRE(runtime::ConsumeAndroidArmSupervisorCall(f.cpu, stop, f.dispatcher) != runtime::SupervisorCallProgress::not_handled);
        CHECK(f.cpu.GetState() == initial);
        CHECK_FALSE(f.dispatcher.signal_binding->runtime->Pending(2));
    }
}

TEST_CASE("ARM signal pending coalesces inherits mask and retires thread state") {
    SignalFixture<cpu::InterpreterCpu> f;
    f.Action(30, 0x10100);
    f.memory.Write32(GuestAddress{0x20080}, 1U << 29);
    REQUIRE(f.Call(126, {2, 0x20080}) == 0);
    REQUIRE(f.Send(30) == 0);
    REQUIRE(f.Send(30) == 0);
    CHECK_FALSE(f.dispatcher.signal_binding->runtime->Pending(2));
    REQUIRE(f.Call(73, {0x20084}) == 0);
    CHECK(f.memory.Read32(GuestAddress{0x20084}) == (1U << 29));
    f.dispatcher.signal_binding->runtime->Inherit(2, 3);
    REQUIRE(f.Call(126, {0, 0, 0x20084}, 3) == 0);
    CHECK(f.memory.Read32(GuestAddress{0x20084}) == (1U << 29));
    REQUIRE(f.Call(73, {0x20084}, 3) == 0);
    CHECK(f.memory.Read32(GuestAddress{0x20084}) == 0);
    REQUIRE(f.Call(126, {1, 0x20080}) == 0);
    CHECK(f.dispatcher.signal_binding->runtime->Pending(2));
    f.dispatcher.signal_binding->runtime->Retire(2);
    CHECK_FALSE(f.dispatcher.signal_binding->runtime->Pending(2));
}

TEST_CASE_TEMPLATE("ARM signals suspend and resume a real blocked guest thread", Cpu,
                   cpu::InterpreterCpu, cpu::DynarmicCpu) {
    SignalFixture<Cpu> f;
    f.Code(0x10000, {0xef000000, 0xef000001}); // futex wait, call return
    f.Code(0x10100, {
        0xe59f301c, // ldr r3,[pc,#28] -> marker
        0xe3a00001, 0xe5830000, // marker=1
        0xe3a07048, // r7=sigsuspend
        0xe59f2010, // ldr r2,[pc,#16] -> temporary mask
        0xef000000,
        0xe3a00002, 0xe5830000, 0xe12fff1e,
        0x20090, 0xff7fffff // permit SIGXCPU (24)
    });
    f.Code(0x10200, {0xe12fff1e});
    f.Action(30, 0x10100, 0x10000000, 0xffffffff);
    f.Action(24, 0x10200);
    f.memory.Write32(GuestAddress{0x20080}, 44);
    auto state = f.cpu.GetState(); state.SetRegister(CoreRegister::r7, 240); f.cpu.SetState(state);
    auto worker = std::async(std::launch::async, [&] {
        runtime::A32GuestCallFrame call;
        call.target = GuestAddress{0x10000}; call.registers = {0x20080, 128, 44, 0};
        return runtime::InvokeA32GuestCall(f.cpu, f.dispatcher, f.lifecycle, f.memory,
            call, GuestAddress{0x24000}, GuestAddress{0x10004}, 1000000);
    });
    const auto waiting = Await([&] { return f.futex.WaiterCount(GuestAddress{0x20080}) == 1; });
    CHECK(waiting);
    CHECK(f.Send(30) == 0);
    const auto suspended = Await([&] { return f.memory.Read32(GuestAddress{0x20090}) == 1; });
    CHECK(suspended);
    CHECK(f.Send(24) == 0);
    const auto resumed = Await([&] { return f.memory.Read32(GuestAddress{0x20090}) == 2 && f.futex.WaiterCount(GuestAddress{0x20080}) == 1; });
    CHECK(resumed);
    if (resumed) CHECK(f.futex.Wake(GuestAddress{0x20080}, 1) == 1);
    if (!waiting || !suspended || !resumed) {
        f.lifecycle.RequestExitGroup(1, 1);
        static_cast<void>(f.futex.InterruptAll());
    }
    REQUIRE(worker.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(worker.get().return_value == 0);
    CHECK_FALSE(f.dispatcher.signal_binding->runtime->Pending(2));
}


TEST_CASE("ARM signal alternate stack nesting and guest context edits are honored") {
    SignalFixture<cpu::InterpreterCpu> f;
    f.Code(0x10100, {0xe12fff1e});
    f.Action(30, 0x10100, 0x48000004); // INFO | ONSTACK | NODEFER
    f.memory.Write32(GuestAddress{0x20080}, 0x21000);
    f.memory.Write32(GuestAddress{0x20084}, 0);
    f.memory.Write32(GuestAddress{0x20088}, 4096);
    REQUIRE(f.Call(186, {0x20080}) == 0);
    REQUIRE(f.Send(30) == 0);
    f.dispatcher.signal_binding->runtime->Deliver(f.cpu);
    const auto first = f.cpu.GetState();
    CHECK(first.Register(CoreRegister::sp) >= 0x21000);
    CHECK(first.Register(CoreRegister::sp) < 0x22000);
    runtime::A32SyscallFrame alt;
    alt.number = 186; alt.thread_id = 2; alt.arguments = {0, 0x200a0}; alt.cpu_state = first;
    CHECK(f.dispatcher.Dispatch(alt) == 0);
    CHECK(f.memory.Read32(GuestAddress{0x200a4}) == 1);
    alt.arguments[0] = 0x20080;
    CHECK(f.dispatcher.Dispatch(alt) == -1);
    REQUIRE(f.Send(30) == 0);
    f.dispatcher.signal_binding->runtime->Deliver(f.cpu);
    CHECK(f.cpu.GetState().Register(CoreRegister::sp) < first.Register(CoreRegister::sp));
    auto stopped = f.cpu.Run(20);
    REQUIRE(runtime::ConsumeAndroidArmSupervisorCall(f.cpu, stopped, f.dispatcher) != runtime::SupervisorCallProgress::not_handled);
    CHECK(f.cpu.GetState() == first);
    // SA_SIGINFO consumers may intentionally edit the saved machine context.
    const GuestAddress context{first.Register(CoreRegister::r2)};
    f.memory.Write32(context.Add(32), 88);
    f.memory.Write32(context.Add(240), 0x76543210);
    stopped = f.cpu.Run(20);
    REQUIRE(runtime::ConsumeAndroidArmSupervisorCall(f.cpu, stopped, f.dispatcher) != runtime::SupervisorCallProgress::not_handled);
    CHECK(f.cpu.GetState().Register(CoreRegister::r0) == 88);
    CHECK(f.cpu.GetState().ExtendedRegisters()[0] == 0x76543210);
    CHECK(f.cpu.GetState().Register(CoreRegister::sp) == 0x24000);
}

TEST_CASE("ARM signal nonrestart wait returns EINTR and default abort cancels waiters") {
    SignalFixture<cpu::InterpreterCpu> f;
    f.Code(0x10000, {0xef000000, 0xef000001});
    f.Code(0x10100, {0xe12fff1e});
    f.Action(30, 0x10100);
    f.memory.Write32(GuestAddress{0x20080}, 44);
    auto state = f.cpu.GetState(); state.SetRegister(CoreRegister::r7, 240); f.cpu.SetState(state);
    auto worker = std::async(std::launch::async, [&] {
        runtime::A32GuestCallFrame call;
        call.target = GuestAddress{0x10000}; call.registers = {0x20080, 128, 44, 0};
        return runtime::InvokeA32GuestCall(f.cpu, f.dispatcher, f.lifecycle, f.memory,
            call, GuestAddress{0x24000}, GuestAddress{0x10004}, 100000);
    });
    CHECK(Await([&] { return f.futex.WaiterCount(GuestAddress{0x20080}) == 1; }));
    CHECK(f.Send(30) == 0);
    const bool finished = worker.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    if (!finished) { f.lifecycle.RequestExitGroup(1, 1); static_cast<void>(f.futex.InterruptAll()); }
    REQUIRE(finished);
    CHECK(worker.get().return_value == 0xfffffffcU);
    auto blocked = std::async(std::launch::async, [&] { return f.Call(240, {0x20080, 128, 44, 0}); });
    CHECK(Await([&] { return f.futex.WaiterCount(GuestAddress{0x20080}) == 1; }));
    CHECK(f.Send(6) == 0);
    const bool cancelled = blocked.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    if (!cancelled) static_cast<void>(f.futex.InterruptAll());
    CHECK(cancelled);
    CHECK(blocked.get() == -4);
    CHECK(f.lifecycle.State(2).exit_code == 134);
}

TEST_CASE("ARM signal rt suspend restores old mask and cancels on shutdown") {
    SignalFixture<cpu::InterpreterCpu> f;
    f.Code(0x10100, {0xe12fff1e});
    f.Action(24, 0x10100);
    f.memory.Write64(GuestAddress{0x20080}, 1U << 23);
    REQUIRE(f.Call(175, {2, 0x20080, 0, 8}) == 0);
    REQUIRE(f.Send(24) == 0);
    f.memory.Write64(GuestAddress{0x20088}, 0);
    CHECK(f.Call(179, {0x20088, 4}) == -22);
    CHECK(f.Call(179, {0x20088, 8}) == -4);
    f.dispatcher.signal_binding->runtime->Deliver(f.cpu);
    auto stopped = f.cpu.Run(20);
    REQUIRE(runtime::ConsumeAndroidArmSupervisorCall(f.cpu, stopped, f.dispatcher) != runtime::SupervisorCallProgress::not_handled);
    REQUIRE(f.Call(175, {0, 0, 0x20088, 8}) == 0);
    CHECK(f.memory.Read64(GuestAddress{0x20088}) == (1U << 23));
    auto waiting = std::async(std::launch::async, [&] { return f.Call(72, {0, 0, 0xffffffff}); });
    f.lifecycle.RequestExit(2, 0);
    REQUIRE(waiting.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(waiting.get() == -4);
}
