#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>

#include "ogplay/cpu/dynarmic.h"
#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay_m1_guest/sample.h"
#include "ogplay/runtime/syscall/syscall.h"
#include "ogplay/runtime/syscall/syscall_bridge.h"

namespace {

namespace sample = ogplay::samples::m1_guest;

class ThreadObserver final : public ogplay::memory::MemoryAccessObserver {
public:
    void OnMemoryAccess(const ogplay::memory::BusAccess& access) override {
        accesses.push_back(access);
    }

    std::vector<ogplay::memory::BusAccess> accesses;
};

struct Outcome final {
    ogplay::cpu::RunResult result;
    ogplay::cpu::A32State state;
    std::uint32_t output{};
    std::uint32_t sequence{};
    bool accesses_have_thread_id{};
};

template <typename CpuType, typename Instruction, std::size_t Size>
[[nodiscard]] Outcome ExecuteSample(const std::array<Instruction, Size>& program,
                                    const ogplay::cpu::ExecutionState execution_state,
                                    const std::uint32_t input,
                                    const std::uint32_t sequence,
                                    const std::uint64_t thread_id) {
    const ogplay::memory::GuestAddress code{sample::kCodeAddress};
    const ogplay::memory::GuestAddress mailbox{sample::kMailboxAddress};
    ogplay::memory::AddressSpace memory;
    ThreadObserver observer;
    ogplay::memory::CheckedMemoryBus bus(memory, &observer);
    memory.Map({code, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    memory.Map({mailbox, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);

    auto address = code;
    for (const auto instruction : program) {
        if constexpr (sizeof(Instruction) == sizeof(std::uint32_t)) {
            bus.Write32(address, instruction);
        } else {
            bus.Write16(address, instruction);
        }
        address = address.Add(sizeof(Instruction));
    }
    bus.Write32(mailbox.Add(sample::kInputOffset), input);
    bus.Write32(mailbox.Add(sample::kOutputOffset), 0);
    bus.Write32(mailbox.Add(sample::kSequenceOffset), sequence);
    memory.Protect({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::execute);
    observer.accesses.clear();

    CpuType cpu(bus);
    ogplay::cpu::A32State initial_state;
    initial_state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
    initial_state.SetRegister(ogplay::cpu::CoreRegister::r4, mailbox.Value());
    initial_state.SetState(execution_state);
    initial_state.SetThreadId(thread_id);
    cpu.SetState(initial_state);
    const auto result = cpu.Run(32);
    const bool has_thread_id = std::all_of(
        observer.accesses.begin(), observer.accesses.end(),
        [thread_id](const auto& access) { return access.thread_id == thread_id; });
    return {result, cpu.GetState(),
            bus.Read32(mailbox.Add(sample::kOutputOffset)),
            bus.Read32(mailbox.Add(sample::kSequenceOffset)), has_thread_id};
}

void CheckEquivalent(const Outcome& reference, const Outcome& jit) {
    CHECK(jit.result.reason == reference.result.reason);
    CHECK(jit.result.ticks_consumed == reference.result.ticks_consumed);
    CHECK(jit.result.pc == reference.result.pc);
    CHECK(jit.result.instruction == reference.result.instruction);
    CHECK(jit.result.immediate == reference.result.immediate);
    CHECK(jit.state == reference.state);
    CHECK(jit.output == reference.output);
    CHECK(jit.sequence == reference.sequence);
    CHECK(jit.accesses_have_thread_id);
}

}  // namespace

TEST_CASE("Dynarmic invalidation interrupts an active peer and resumes patched code") {
    using namespace ogplay;
    memory::AddressSpace memory;
    const memory::GuestAddress code{0x10000U};
    memory.Map({code, memory.PageSize()}, memory::PageProtection::read |
        memory::PageProtection::write | memory::PageProtection::execute);
    memory::CheckedMemoryBus bus(memory);
    bus.Write32(code, 0xe3a00001U); // mov r0, #1
    bus.Write32(code.Add(4), 0xef000001U);
    bus.Write32(code.Add(64), 0xef000002U); // blocking host hook
    bus.Write32(code.Add(68), 0xeaffffedU); // b code
    auto context = std::make_shared<cpu::DynarmicExecutionContext>(2);
    cpu::DynarmicCpu publisher(bus, context), peer(bus, context);
    cpu::A32State state;
    state.SetRegister(cpu::CoreRegister::pc, code.Value());
    peer.SetState(state);
    REQUIRE(peer.Run(16).reason == cpu::RunStopReason::supervisor_call);
    REQUIRE(peer.GetState().Register(cpu::CoreRegister::r0) == 1);
    struct Hook final {
        std::promise<void> entered;
        std::promise<void> release;
        std::future<void> released{release.get_future()};
    } hook;
    auto entered = hook.entered.get_future();
    peer.SetHostCallHook({+[](void* userdata, std::uint32_t svc,
                            cpu::A32HostCallContext&) noexcept {
        if (svc != 2) return cpu::HostCallResult::unhandled;
        auto& h = *static_cast<Hook*>(userdata);
        h.entered.set_value();
        h.released.wait();
        return cpu::HostCallResult::handled;
    }, &hook});
    state.SetRegister(cpu::CoreRegister::pc, code.Add(64).Value());
    peer.SetState(state);
    auto result = std::async(std::launch::async, [&] { return peer.Run(64); });
    const auto ready = entered.wait_for(std::chrono::seconds(2));
    if (ready != std::future_status::ready) {
        peer.RequestHalt();
        hook.release.set_value();
        static_cast<void>(result.get());
        REQUIRE(ready == std::future_status::ready);
        return;
    }
    // The peer is inside its JIT callback, but no memory fetch is in flight.
    bus.Write32(code, 0xe3a00002U);
    SUBCASE("explicit code cacheflush") { publisher.InvalidateCodeRange({code, 4}); }
    SUBCASE("mapping permission publication") {
        memory.Protect({code, memory.PageSize()}, memory::PageProtection::read |
            memory::PageProtection::write | memory::PageProtection::execute);
    }
    hook.release.set_value();
    auto stop = result.get();
    if (stop.reason == cpu::RunStopReason::budget_exhausted) stop = peer.Run(64);
    REQUIRE(stop.reason == cpu::RunStopReason::supervisor_call);
    CHECK(stop.immediate == 1);
    CHECK(peer.GetState().Register(cpu::CoreRegister::r0) == 2);
}

TEST_CASE("Dynarmic matches the interpreter on the M1 bare guest samples") {
    SUBCASE("A32") {
        const auto reference = ExecuteSample<ogplay::cpu::InterpreterCpu>(
            sample::kA32Program, ogplay::cpu::ExecutionState::a32, 19, 41, 301);
        const auto jit = ExecuteSample<ogplay::cpu::DynarmicCpu>(
            sample::kA32Program, ogplay::cpu::ExecutionState::a32, 19, 41, 301);
        CheckEquivalent(reference, jit);
    }
    SUBCASE("Thumb") {
        const auto reference = ExecuteSample<ogplay::cpu::InterpreterCpu>(
            sample::kThumbProgram, ogplay::cpu::ExecutionState::thumb, 35, 8, 302);
        const auto jit = ExecuteSample<ogplay::cpu::DynarmicCpu>(
            sample::kThumbProgram, ogplay::cpu::ExecutionState::thumb, 35, 8, 302);
        CheckEquivalent(reference, jit);
    }
}

TEST_CASE("Dynarmic exposes deterministic budget and halt stops") {
    const std::array<std::uint32_t, 2> program{0xe1a00000, 0xeafffffd};
    const auto budget = ExecuteSample<ogplay::cpu::DynarmicCpu>(
        program, ogplay::cpu::ExecutionState::a32, 0, 0, 303);
    CHECK(budget.result.reason == ogplay::cpu::RunStopReason::budget_exhausted);
    CHECK(budget.result.ticks_consumed == 32);

    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus(memory);
    ogplay::cpu::DynarmicCpu cpu(bus);
    cpu.RequestHalt();
    const auto halt = cpu.Run(4);
    CHECK(halt.reason == ogplay::cpu::RunStopReason::halt_requested);
    CHECK(halt.ticks_consumed == 0);
}

TEST_CASE("Dynarmic host call hook handles falls back and faults") {
    const ogplay::memory::GuestAddress code{sample::kCodeAddress};
    ogplay::memory::AddressSpace memory;
    memory.Map({code, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus(memory);
    bus.Write16(code, 0xdf02U);         // svc #2
    bus.Write16(code.Add(2), 0xdf01U);  // svc #1
    memory.Protect({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::execute);
    ogplay::cpu::DynarmicCpu cpu(bus);
    ogplay::cpu::A32State state;
    state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r0, 40U);
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    state.SetThreadId(390U);
    cpu.SetState(state);

    struct HookState final {
        ogplay::cpu::HostCallResult result{
            ogplay::cpu::HostCallResult::handled};
        std::uint32_t calls{};
    } hook_state;
    cpu.SetHostCallHook({
        +[](void* userdata, const std::uint32_t svc,
            ogplay::cpu::A32HostCallContext& call) noexcept {
            auto& hook = *static_cast<HookState*>(userdata);
            if (svc != 2U) return ogplay::cpu::HostCallResult::unhandled;
            ++hook.calls;
            CHECK(call.thread_id == 390U);
            CHECK(call.pc ==
                  ogplay::memory::GuestAddress{sample::kCodeAddress});
            call.registers[0] += 2U;
            return hook.result;
        },
        &hook_state});
    const auto handled = cpu.Run(8);
    CHECK(handled.reason == ogplay::cpu::RunStopReason::supervisor_call);
    CHECK(handled.immediate == 1U);
    CHECK(cpu.GetState().Register(ogplay::cpu::CoreRegister::r0) == 42U);
    CHECK(hook_state.calls == 1U);

    state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    cpu.SetState(state);
    hook_state.result = ogplay::cpu::HostCallResult::unhandled;
    CHECK(cpu.Run(4).reason == ogplay::cpu::RunStopReason::supervisor_call);

    state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    cpu.SetState(state);
    hook_state.result = ogplay::cpu::HostCallResult::fault;
    CHECK(cpu.Run(4).reason == ogplay::cpu::RunStopReason::host_call_fault);
}

TEST_CASE("Dynarmic exposes the guest thread pointer through TPIDRURO") {
    const std::array<std::uint32_t, 2> program{0xee1d2f70, 0xef000001};
    const ogplay::memory::GuestAddress code{sample::kCodeAddress};
    ogplay::memory::AddressSpace memory;
    memory.Map({code, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus(memory);
    bus.Write32(code, program[0]);
    bus.Write32(code.Add(4), program[1]);
    memory.Protect({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::execute);
    ogplay::cpu::DynarmicCpu cpu(bus);
    ogplay::cpu::A32State state;
    state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
    state.SetThreadPointer(ogplay::memory::GuestAddress{0x56789000U});
    cpu.SetState(state);
    CHECK(cpu.Run(4).reason == ogplay::cpu::RunStopReason::supervisor_call);
    const auto result = cpu.GetState();
    CHECK(result.Register(ogplay::cpu::CoreRegister::r2) == 0x56789000U);
    CHECK(result.ThreadPointer() ==
          ogplay::memory::GuestAddress{0x56789000U});
}

TEST_CASE("Dynarmic exclusive store preserves a peer direct unlock") {
    using namespace ogplay;
    memory::AddressSpace space;
    const memory::GuestAddress code{0x10000}, flag{0x20000};
    const auto rw = memory::PageProtection::read | memory::PageProtection::write;
    space.Map({code, 4096}, rw);
    space.Map({flag, 4096}, rw);
    memory::CheckedMemoryBus direct(space);
    direct.Write32(code, 0xe1902f9fU);        // ldrex r2, [r0]
    direct.Write32(code.Add(4), 0xe1803f91U); // strex r3, r1, [r0]
    direct.Write32(code.Add(8), 0xef000001U);
    direct.Write32(code.Add(64), 0xe5801000U); // str r1, [r0]
    direct.Write32(code.Add(68), 0xef000001U);
    direct.Write32(flag, 1);
    space.Protect({code, 4096}, memory::PageProtection::read | memory::PageProtection::execute);
    auto context = std::make_shared<cpu::DynarmicExecutionContext>(2);
    cpu::DynarmicCpu owner(direct, context);
    struct InterleavingBus final : memory::MemoryBus {
        memory::CheckedMemoryBus& bus;
        cpu::DynarmicCpu& owner;
        memory::GuestAddress flag;
        unsigned reads{};
        bool owner_ran{};
        InterleavingBus(memory::CheckedMemoryBus& b, cpu::DynarmicCpu& c,
                        memory::GuestAddress f) : bus(b), owner(c), flag(f) {}
        void Unlock() {
            REQUIRE_FALSE(owner_ran);
            REQUIRE(owner.Run(4).reason == cpu::RunStopReason::supervisor_call);
            REQUIRE(bus.Read32(flag) == 0);
            owner_ran = true;
        }
        std::uint32_t Read32(memory::GuestAddress a, std::uint64_t t) override {
            const auto value = bus.Read32(a, t);
            // The old implementation's second read was the comparison inside
            // STREX. A direct STR here must never be overwritten by that STREX.
            if (a == flag && ++reads == 2) Unlock();
            return value;
        }
        bool CompareExchange32(memory::GuestAddress a, std::uint32_t expected,
                               std::uint32_t value, std::uint64_t t) override {
            Unlock();
            return bus.CompareExchange32(a, expected, value, t);
        }
        std::uint8_t Read8(memory::GuestAddress a, std::uint64_t t) override { return bus.Read8(a, t); }
        std::uint16_t Read16(memory::GuestAddress a, std::uint64_t t) override { return bus.Read16(a, t); }
        std::uint64_t Read64(memory::GuestAddress a, std::uint64_t t) override { return bus.Read64(a, t); }
        std::uint16_t Fetch16(memory::GuestAddress a, std::uint64_t t) override { return bus.Fetch16(a, t); }
        std::uint32_t Fetch32(memory::GuestAddress a, std::uint64_t t) override { return bus.Fetch32(a, t); }
        void Write8(memory::GuestAddress a, std::uint8_t v, std::uint64_t t) override { bus.Write8(a, v, t); }
        void Write16(memory::GuestAddress a, std::uint16_t v, std::uint64_t t) override { bus.Write16(a, v, t); }
        void Write32(memory::GuestAddress a, std::uint32_t v, std::uint64_t t) override { bus.Write32(a, v, t); }
        void Write64(memory::GuestAddress a, std::uint64_t v, std::uint64_t t) override { bus.Write64(a, v, t); }
    } interleaved(direct, owner, flag);
    cpu::DynarmicCpu contender(interleaved, context);
    cpu::A32State initial;
    initial.SetRegister(cpu::CoreRegister::pc, code.Add(64).Value());
    initial.SetRegister(cpu::CoreRegister::r0, flag.Value());
    initial.SetRegister(cpu::CoreRegister::r1, 0);
    owner.SetState(initial);
    initial.SetRegister(cpu::CoreRegister::pc, code.Value());
    initial.SetRegister(cpu::CoreRegister::r1, 1);
    contender.SetState(initial);
    REQUIRE(contender.Run(8).reason == cpu::RunStopReason::supervisor_call);
    REQUIRE(interleaved.owner_ran);
    CHECK(contender.GetState().Register(cpu::CoreRegister::r2) == 1);
    CHECK(contender.GetState().Register(cpu::CoreRegister::r3) == 1);
    CHECK(direct.Read32(flag) == 0);
}

TEST_CASE("Dynarmic executes ARM exclusive memory operations") {
    const ogplay::memory::GuestAddress code{sample::kCodeAddress};
    const ogplay::memory::GuestAddress counter{sample::kMailboxAddress};
    ogplay::memory::AddressSpace memory;
    memory.Map({code, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    memory.Map({counter, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus(memory);
    bus.Write32(code, 0xe1901f9fU);         // ldrex r1, [r0]
    bus.Write32(code.Add(4), 0xe2811001U);  // add r1, r1, #1
    bus.Write32(code.Add(8), 0xe1802f91U);  // strex r2, r1, [r0]
    bus.Write32(code.Add(12), 0xef000001U); // svc #1
    bus.Write32(counter, 9);
    memory.Protect({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::execute);

    auto context =
        std::make_shared<ogplay::cpu::DynarmicExecutionContext>(2);
    ogplay::cpu::DynarmicCpu cpu(bus, context);
    ogplay::cpu::A32State state;
    state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r0, counter.Value());
    cpu.SetState(state);
    CHECK(cpu.Run(8).reason == ogplay::cpu::RunStopReason::supervisor_call);
    CHECK(cpu.GetState().Register(ogplay::cpu::CoreRegister::r2) == 0);
    CHECK(bus.Read32(counter) == 10);
    CHECK_THROWS_AS(
        ogplay::cpu::DynarmicCpu(
            bus, std::shared_ptr<ogplay::cpu::DynarmicExecutionContext>{}),
        std::invalid_argument);
    ogplay::cpu::DynarmicCpu second_cpu(bus, context);
    CHECK_THROWS_AS(ogplay::cpu::DynarmicCpu(bus, context),
                    std::runtime_error);
}

TEST_CASE("Dynarmic exclusive direct and observed paths preserve all scalar widths") {
    using namespace ogplay;
    memory::AddressSpace space;
    const memory::GuestAddress code{0x10000}, data{0x20000};
    const auto rw = memory::PageProtection::read | memory::PageProtection::write;
    space.Map({code, 4096}, rw);
    space.Map({data, 4096}, rw);
    ThreadObserver observer;
    memory::MemoryAccessObserver* selected{};
    SUBCASE("published direct pages") {}
    SUBCASE("observed callbacks") { selected = &observer; }
    memory::CheckedMemoryBus bus(space, selected);
    struct Program { std::uint32_t load, store; unsigned bytes; };
    for (const auto program : {Program{0xe1d02f9fU, 0xe1c04f96U, 1},
                              Program{0xe1f02f9fU, 0xe1e04f96U, 2},
                              Program{0xe1902f9fU, 0xe1804f96U, 4},
                              Program{0xe1b02f9fU, 0xe1a04f96U, 8}}) {
        INFO(program.bytes);
        space.Protect({code, 4096}, rw);
        bus.Write32(code, program.load); // ldrex[b/h/d] r2[,r3], [r0]
        bus.Write32(code.Add(4), 0xef000001U);
        bus.Write32(code.Add(8), program.store); // strex[b/h/d] r4, r6[,r7], [r0]
        bus.Write32(code.Add(12), 0xef000001U);
        space.Protect({code, 4096}, memory::PageProtection::read | memory::PageProtection::execute);
        constexpr auto initial = UINT64_C(0x1122334455667788);
        constexpr auto desired = UINT64_C(0xaabbccddabcdef01);
        const auto mask = program.bytes == 8 ? UINT64_MAX
                         : (UINT64_C(1) << (program.bytes * 8)) - 1;
        bus.Write64(data, initial);
        observer.accesses.clear();
        cpu::DynarmicCpu cpu(bus);
        cpu::A32State state;
        state.SetThreadId(17);
        state.SetRegister(cpu::CoreRegister::pc, code.Value());
        state.SetRegister(cpu::CoreRegister::r0, data.Value());
        state.SetRegister(cpu::CoreRegister::r6, static_cast<std::uint32_t>(desired));
        state.SetRegister(cpu::CoreRegister::r7, static_cast<std::uint32_t>(desired >> 32));
        cpu.SetState(state);
        REQUIRE(cpu.Run(8).reason == cpu::RunStopReason::supervisor_call);
        CHECK(cpu.GetState().Register(cpu::CoreRegister::r2) ==
              static_cast<std::uint32_t>(initial & mask));
        REQUIRE(cpu.Run(8).reason == cpu::RunStopReason::supervisor_call);
        CHECK(cpu.GetState().Register(cpu::CoreRegister::r4) == 0);
        CHECK(bus.Read64(data) == ((initial & ~mask) | (desired & mask)));
        if (selected) {
            CHECK(std::ranges::any_of(observer.accesses, [&](const auto& access) {
                return access.address == data && access.type == memory::BusAccessType::write &&
                       access.size == program.bytes && access.thread_id == 17;
            }));
        }
        // Ordinary peer store between load and exclusive store must win.
        cpu.SetState(state);
        REQUIRE(cpu.Run(8).reason == cpu::RunStopReason::supervisor_call);
        bus.Write64(data, 0);
        REQUIRE(cpu.Run(8).reason == cpu::RunStopReason::supervisor_call);
        CHECK(cpu.GetState().Register(cpu::CoreRegister::r4) == 1);
        CHECK(bus.Read64(data) == 0);
    }
}

TEST_CASE("Dynarmic exclusive locks interoperate with direct unlocks on host threads") {
    using namespace ogplay;
    memory::AddressSpace space;
    const memory::GuestAddress code{0x10000}, flag{0x20000}, counter{0x20004};
    const auto rw = memory::PageProtection::read | memory::PageProtection::write;
    space.Map({code, 4096}, rw);
    space.Map({flag, 4096}, rw);
    memory::CheckedMemoryBus bus(space);
    // Acquire with LDREX/STREX; protect counter with DMB; unlock with a direct
    // STR. Bounded Run budgets ensure a lost unlock fails instead of hanging.
    const std::array program{
        0xe1902f9fU, 0xe3520000U, 0x1afffffcU, 0xe3a01001U,
        0xe1803f91U, 0xe3530000U, 0x1afffff8U, 0xf57ff05fU,
        0xe5942000U, 0xe2822001U, 0xe5842000U, 0xf57ff05fU,
        0xe3a01000U, 0xe5801000U, 0xe2555001U, 0x1affffefU,
        0xef000001U};
    for (std::size_t i = 0; i < program.size(); ++i) bus.Write32(code.Add(i * 4), program[i]);
    space.Protect({code, 4096}, memory::PageProtection::read | memory::PageProtection::execute);
    constexpr std::size_t workers = 4;
    auto context = std::make_shared<cpu::DynarmicExecutionContext>(workers);
    std::array<std::unique_ptr<cpu::DynarmicCpu>, workers> cpus;
    std::array<std::future<cpu::RunResult>, workers> results;
    std::promise<void> start;
    auto ready = start.get_future().share();
    for (std::size_t i = 0; i < workers; ++i) {
        cpus[i] = std::make_unique<cpu::DynarmicCpu>(bus, context);
        cpu::A32State state;
        state.SetThreadId(i + 1);
        state.SetRegister(cpu::CoreRegister::pc, code.Value());
        state.SetRegister(cpu::CoreRegister::r0, flag.Value());
        state.SetRegister(cpu::CoreRegister::r4, counter.Value());
        state.SetRegister(cpu::CoreRegister::r5, 1000);
        cpus[i]->SetState(state);
        results[i] = std::async(std::launch::async, [&, i, ready] {
            ready.wait();
            return cpus[i]->Run(10000000);
        });
    }
    start.set_value();
    for (auto& result : results) CHECK(result.get().reason == cpu::RunStopReason::supervisor_call);
    CHECK(bus.Read32(counter) == workers * 1000);
    CHECK(bus.Read32(flag) == 0);
}

TEST_CASE("Dynarmic exclusive monitor rejects a peer exclusive ABA") {
    using namespace ogplay;
    memory::AddressSpace space;
    const memory::GuestAddress code{0x10000}, flag{0x20000};
    const auto rw = memory::PageProtection::read | memory::PageProtection::write;
    space.Map({code, 4096}, rw);
    space.Map({flag, 4096}, rw);
    memory::CheckedMemoryBus bus(space);
    const std::array first{0xe1902f9fU, 0xef000001U, 0xe1803f91U, 0xef000001U};
    // Peer changes 1 -> 0 -> 1 using exclusive writes. Value comparison alone
    // would miss this interference; the shared monitor must retain its role.
    const std::array peer{0xe1902f9fU, 0xe1803f91U, 0xe1902f9fU,
                          0xe3a01001U, 0xe1803f91U, 0xef000001U};
    for (std::size_t i = 0; i < first.size(); ++i) bus.Write32(code.Add(i * 4), first[i]);
    for (std::size_t i = 0; i < peer.size(); ++i) bus.Write32(code.Add(64 + i * 4), peer[i]);
    bus.Write32(flag, 1);
    space.Protect({code, 4096}, memory::PageProtection::read | memory::PageProtection::execute);
    auto context = std::make_shared<cpu::DynarmicExecutionContext>(2);
    cpu::DynarmicCpu contender(bus, context), writer(bus, context);
    cpu::A32State state;
    state.SetRegister(cpu::CoreRegister::pc, code.Value());
    state.SetRegister(cpu::CoreRegister::r0, flag.Value());
    state.SetRegister(cpu::CoreRegister::r1, 2);
    contender.SetState(state);
    state.SetRegister(cpu::CoreRegister::pc, code.Add(64).Value());
    state.SetRegister(cpu::CoreRegister::r1, 0);
    writer.SetState(state);
    REQUIRE(contender.Run(8).reason == cpu::RunStopReason::supervisor_call);
    REQUIRE(writer.Run(12).reason == cpu::RunStopReason::supervisor_call);
    REQUIRE(bus.Read32(flag) == 1);
    REQUIRE(contender.Run(8).reason == cpu::RunStopReason::supervisor_call);
    CHECK(contender.GetState().Register(cpu::CoreRegister::r3) == 1);
    CHECK(bus.Read32(flag) == 1);
}

TEST_CASE("Dynarmic exclusive store preserves permission fault attribution") {
    using namespace ogplay;
    memory::AddressSpace space;
    const memory::GuestAddress code{0x10000}, flag{0x20000};
    const auto rw = memory::PageProtection::read | memory::PageProtection::write;
    space.Map({code, 4096}, rw);
    space.Map({flag, 4096}, rw);
    memory::CheckedMemoryBus bus(space);
    bus.Write32(code, 0xe1902f9fU);
    bus.Write32(code.Add(4), 0xef000001U);
    bus.Write32(code.Add(8), 0xe1803f91U);
    bus.Write32(code.Add(12), 0xef000001U);
    space.Protect({code, 4096}, memory::PageProtection::read | memory::PageProtection::execute);
    cpu::DynarmicCpu executor(bus);
    cpu::A32State state;
    state.SetThreadId(91);
    state.SetRegister(cpu::CoreRegister::pc, code.Value());
    state.SetRegister(cpu::CoreRegister::r0, flag.Value());
    state.SetRegister(cpu::CoreRegister::r1, 2);
    executor.SetState(state);
    REQUIRE(executor.Run(8).reason == cpu::RunStopReason::supervisor_call);
    space.Protect({flag, 4096}, memory::PageProtection::read);
    const auto result = executor.Run(8);
    REQUIRE(result.reason == cpu::RunStopReason::memory_fault);
    REQUIRE(result.fault.has_value());
    CHECK(result.fault->address == flag);
    CHECK(result.fault->access == memory::AccessType::write);
    CHECK(result.fault->reason == memory::FaultReason::permission_denied);
    CHECK(result.fault->thread_id == 91);
    CHECK(bus.Read32(flag) == 0);
}

TEST_CASE("Dynarmic reports callback-only data memory faults") {
    const ogplay::memory::GuestAddress code{sample::kCodeAddress};
    const ogplay::memory::GuestAddress unmapped{0x30000U};
    ogplay::memory::AddressSpace memory;
    memory.Map({code, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus(memory);
    bus.Write32(code, 0xe5901000U);         // ldr r1, [r0]
    bus.Write32(code.Add(4), 0xef000001U);  // svc #1
    memory.Protect({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::execute);

    ogplay::cpu::DynarmicCpu cpu(bus);
    ogplay::cpu::A32State state;
    state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r0, unmapped.Value());
    state.SetThreadId(304);
    cpu.SetState(state);

    const auto result = cpu.Run(8);
    CHECK(result.reason == ogplay::cpu::RunStopReason::memory_fault);
    REQUIRE(result.fault.has_value());
    CHECK(result.fault->address == unmapped);
    CHECK(result.fault->access == ogplay::memory::AccessType::read);
    CHECK(result.fault->thread_id == 304);
}

TEST_CASE("Dynarmic read pages preserve protected data and live permission changes") {
    using namespace ogplay;
    memory::AddressSpace memory;
    const memory::GuestAddress code{sample::kCodeAddress};
    const memory::GuestAddress data{sample::kMailboxAddress};
    const auto rw = memory::PageProtection::read | memory::PageProtection::write;
    memory.Map({code, memory.PageSize()}, rw);
    memory.Map({data, memory.PageSize() * 2}, rw);
    memory::CheckedMemoryBus bus(memory);
    bus.Write32(code, 0xe5901000U); // ldr r1, [r0]
    bus.Write32(code.Add(4), 0xef000001U);
    bus.Write32(code.Add(8), 0xe5801000U); // str r1, [r0]
    bus.Write32(code.Add(12), 0xef000001U);
    bus.Write32(data, 0x12345678U);
    memory.Protect({code, memory.PageSize()},
                   memory::PageProtection::read | memory::PageProtection::execute);
    memory.Protect({data, memory.PageSize()}, memory::PageProtection::read);
    cpu::DynarmicCpu cpu(bus);
    cpu::A32State state;
    state.SetThreadId(304);
    state.SetRegister(cpu::CoreRegister::r0, data.Value());
    const auto run = [&](const memory::GuestAddress pc) {
        state.SetRegister(cpu::CoreRegister::pc, pc.Value());
        cpu.SetState(state);
        return cpu.Run(8);
    };
    REQUIRE(run(code).reason == cpu::RunStopReason::supervisor_call);
    CHECK(cpu.GetState().Register(cpu::CoreRegister::r1) == 0x12345678U);
    // The readable table must never grant writes to a read-only page.
    state.SetRegister(cpu::CoreRegister::r1, 0xffffffffU);
    auto result = run(code.Add(8));
    REQUIRE(result.fault.has_value());
    CHECK(result.fault->access == memory::AccessType::write);
    CHECK(result.fault->reason == memory::FaultReason::permission_denied);
    CHECK(bus.Read32(data) == 0x12345678U);
    // Same CPU and cached load, now from executable/readable data.
    memory.Protect({data, memory.PageSize()},
                   memory::PageProtection::read | memory::PageProtection::execute);
    REQUIRE(run(code).reason == cpu::RunStopReason::supervisor_call);
    CHECK(cpu.GetState().Register(cpu::CoreRegister::r1) == 0x12345678U);
    // Execute permission alone cannot authorize data reads.
    memory.Protect({data, memory.PageSize()}, memory::PageProtection::execute);
    result = run(code);
    REQUIRE(result.fault.has_value());
    CHECK(result.fault->access == memory::AccessType::read);
    CHECK(result.fault->thread_id == 304);
    // Cross-page reads must validate the second page before reading it.
    memory.Protect({data, memory.PageSize()}, memory::PageProtection::read);
    memory.Protect({data.Add(memory.PageSize()), memory.PageSize()}, memory::PageProtection::none);
    state.SetRegister(cpu::CoreRegister::r0, data.Add(memory.PageSize() - 2).Value());
    result = run(code);
    REQUIRE(result.fault.has_value());
    CHECK(result.fault->address == data.Add(memory.PageSize()));
    memory.Unmap({data, memory.PageSize()});
    state.SetRegister(cpu::CoreRegister::r0, data.Value());
    result = run(code);
    REQUIRE(result.fault.has_value());
    CHECK(result.fault->reason == memory::FaultReason::unmapped);
}

TEST_CASE("Dynarmic direct memory falls back for cross-page permission checks") {
    const ogplay::memory::GuestAddress code{sample::kCodeAddress};
    const ogplay::memory::GuestAddress data{sample::kMailboxAddress};
    ogplay::memory::AddressSpace memory;
    const auto read_write = ogplay::memory::PageProtection::read |
                            ogplay::memory::PageProtection::write;
    memory.Map({code, memory.PageSize()}, read_write);
    memory.Map({data, memory.PageSize() * 2U}, read_write);
    memory.Protect({data.Add(memory.PageSize()), memory.PageSize()},
                   ogplay::memory::PageProtection::read);
    ogplay::memory::CheckedMemoryBus bus(memory);
    bus.Write32(code, 0xe5801000U);         // str r1, [r0]
    bus.Write32(code.Add(4), 0xef000001U);  // svc #1
    memory.Protect({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::execute);

    ogplay::cpu::DynarmicCpu cpu(bus);
    ogplay::cpu::A32State state;
    const auto crossing = data.Add(memory.PageSize() - 2U);
    state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r0, crossing.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r1, 0xaabbccddU);
    state.SetThreadId(305);
    cpu.SetState(state);

    const auto result = cpu.Run(8);
    CHECK(result.reason == ogplay::cpu::RunStopReason::memory_fault);
    REQUIRE(result.fault.has_value());
    CHECK(result.fault->access == ogplay::memory::AccessType::write);
    CHECK(result.fault->thread_id == 305);
    CHECK(bus.Read16(crossing) == 0);
}

TEST_CASE("Dashboard source Dynarmic publishes owner thread cache usage") {
#if defined(_WIN32) && defined(_M_X64)
    using namespace ogplay;
    memory::AddressSpace memory; memory::CheckedMemoryBus bus(memory);
    const memory::GuestAddress address{0x10000};
    memory.Map({address,4096},memory::PageProtection::read | memory::PageProtection::write);
    memory.Write32(address,0xeafffffeU);
    memory.Protect({address,4096},memory::PageProtection::read | memory::PageProtection::execute);
    auto context=std::make_shared<cpu::DynarmicExecutionContext>(2);
    cpu::DynarmicCpu jit(bus,context); cpu::A32State state; state.SetRegister(cpu::CoreRegister::pc,address.Value()); jit.SetState(state);
    static_cast<void>(jit.Run(10)); const auto snapshot=context->TrySnapshot(); REQUIRE(snapshot); REQUIRE(snapshot->size()==1);
    CHECK((*snapshot)[0].capacity_bytes==64U*1024U*1024U); CHECK((*snapshot)[0].used_bytes>0);
    CHECK((*snapshot)[0].used_bytes<(*snapshot)[0].capacity_bytes); CHECK((*snapshot)[0].captured_at_steady_ns>0);
#endif
}

namespace {
struct CoprocessorGuest final {
    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus{memory};
    ogplay::cpu::DynarmicCpu cpu{bus};
    ogplay::cpu::A32State state;
    const ogplay::memory::GuestAddress code{sample::kCodeAddress};
    const ogplay::memory::GuestAddress data{sample::kMailboxAddress};

    CoprocessorGuest(const std::vector<std::uint32_t>& instructions, bool thumb,
                     std::uint32_t flags = 0) {
        using ogplay::memory::PageProtection;
        memory.Map({code, memory.PageSize()}, PageProtection::read | PageProtection::write);
        memory.Map({data, memory.PageSize()}, PageProtection::read | PageProtection::write);
        auto pc = code;
        for (auto instruction : instructions) {
            if (thumb) {
                if (instruction > 0xffff) {
                    bus.Write16(pc, static_cast<std::uint16_t>(instruction >> 16));
                    pc = pc.Add(2);
                }
                bus.Write16(pc, static_cast<std::uint16_t>(instruction));
                pc = pc.Add(2);
            } else {
                bus.Write32(pc, instruction);
                pc = pc.Add(4);
            }
        }
        memory.Protect({code, memory.PageSize()}, PageProtection::read | PageProtection::execute);
        state.SetCpsr(flags);
        state.SetState(thumb ? ogplay::cpu::ExecutionState::thumb : ogplay::cpu::ExecutionState::a32);
        state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
        state.SetRegister(ogplay::cpu::CoreRegister::r4, data.Value());
        state.SetThreadPointer(ogplay::memory::GuestAddress{0x56789000});
        state.SetThreadId(444);
        cpu.SetState(state);
    }
};
} // namespace

TEST_CASE("Dynarmic BND-44 legacy and modern barriers preserve ARM and Thumb execution") {
    using namespace ogplay::cpu;
    for (bool thumb : {false, true}) {
        for (auto opcode : {0xee073fbaU, 0xee073f9aU, 0xee073f95U}) {
            CAPTURE(thumb);
            CAPTURE(opcode);
            CoprocessorGuest guest({opcode, thumb ? 0x2007U : 0xe3a00007U,
                                   thumb ? 0xdf01U : 0xef000001U}, thumb);
            const auto stop = guest.cpu.Run(32);
            CHECK(stop.reason == RunStopReason::supervisor_call);
            CHECK(guest.cpu.GetState().Register(CoreRegister::r0) == 7);
            CHECK(guest.cpu.GetState().State() == guest.state.State());
        }
        for (auto opcode : {thumb ? 0xf3bf8f5fU : 0xf57ff05fU,
                            thumb ? 0xf3bf8f4fU : 0xf57ff04fU,
                            thumb ? 0xf3bf8f6fU : 0xf57ff06fU}) {
            CoprocessorGuest guest({opcode, thumb ? 0xdf01U : 0xef000001U}, thumb);
            CHECK(guest.cpu.Run(32).reason == RunStopReason::supervisor_call);
        }
    }
}

TEST_CASE("Dynarmic BND-44 unsupported coprocessor families stop without host abort") {
    using namespace ogplay::cpu;
    // MCR/MRC/CDP/MCRR/MRRC/LDC/STC, including absent CP14 and CP15.
    // Loads/stores request writeback: neither memory access nor writeback is allowed.
    for (bool thumb : {false, true}) {
        for (auto opcode : {0xee000f10U, 0xee100f10U, 0xee000e00U,
                            0xec410f00U, 0xec510f00U, 0xedb00f01U, 0xeda00f01U,
                            0xfe000f10U, 0xfe100f10U, 0xfe000e00U,
                            0xfc410f00U, 0xfc510f00U, 0xfdb00f01U, 0xfda00f01U}) {
            CAPTURE(thumb);
            CAPTURE(opcode);
            CoprocessorGuest guest({opcode, thumb ? 0x2009U : 0xe3a00009U,
                                   thumb ? 0xdf01U : 0xef000001U}, thumb);
            const auto stop = guest.cpu.Run(32);
            REQUIRE(stop.reason == RunStopReason::unsupported_instruction);
            CHECK(stop.pc == guest.code);
            CHECK(stop.instruction == opcode);
            CHECK(guest.cpu.GetState().Register(CoreRegister::pc) == guest.code.Value());
            CHECK(guest.cpu.GetState().Register(CoreRegister::r0) == 0);
            CHECK(guest.cpu.GetState().ThreadId() == 444);
            CHECK_FALSE(stop.fault.has_value());
            CHECK(guest.cpu.Run(32).reason == RunStopReason::unsupported_instruction);
        }
    }
}

TEST_CASE("Dynarmic BND-44 ARM conditions and precise unsupported side effects") {
    using namespace ogplay::cpu;
    // A preceding store must commit; the subsequent store must not execute.
    CoprocessorGuest guest({0xe3a00007U, 0xe5840000U, 0xee000f10U,
                           0xe3a00009U, 0xe5840000U, 0xef000001U}, false);
    const auto stop = guest.cpu.Run(32);
    REQUIRE(stop.reason == RunStopReason::unsupported_instruction);
    CHECK(stop.pc == guest.code.Add(8));
    CHECK(stop.instruction == 0xee000f10U);
    CHECK(guest.bus.Read32(guest.data) == 7);
    CHECK(guest.cpu.GetState().Register(CoreRegister::r0) == 7);

    for (auto opcode : {0x0e000f10U, 0x0e073f95U}) { // EQ unsupported / ISB
        CoprocessorGuest skipped({0xe3a00007U, opcode, 0xe2800001U, 0xef000001U}, false);
        CHECK(skipped.cpu.Run(32).reason == RunStopReason::supervisor_call);
        CHECK(skipped.cpu.GetState().Register(CoreRegister::r0) == 8);
    }
    CoprocessorGuest taken({0x0e000f10U, 0xef000001U}, false, 1U << 30);
    CHECK(taken.cpu.Run(32).reason == RunStopReason::unsupported_instruction);
}

TEST_CASE("Dynarmic BND-44 Thumb IT state survives barriers and unsupported stops") {
    using namespace ogplay::cpu;
    for (auto opcode : {0xee000f10U, 0xee073fbaU, 0xee073f9aU, 0xee073f95U}) {
        CAPTURE(opcode);
        CoprocessorGuest skipped({0x2000U, 0x2801U, 0xbf08U, opcode, 0x2109U, 0xdf01U}, true);
        CHECK(skipped.cpu.Run(32).reason == RunStopReason::supervisor_call);
        CHECK(skipped.cpu.GetState().Register(CoreRegister::r1) == 9);
    }
    // ITE EQ: execute the barrier and skip the NE MOV, including after ISB dispatch.
    for (auto opcode : {0xee073fbaU, 0xee073f9aU, 0xee073f95U}) {
        CoprocessorGuest taken({0x2000U, 0x2800U, 0xbf0cU, opcode, 0x2109U, 0xdf01U}, true);
        CHECK(taken.cpu.Run(32).reason == RunStopReason::supervisor_call);
        CHECK(taken.cpu.GetState().Register(CoreRegister::r1) == 0);
    }
    CoprocessorGuest fault({0x2000U, 0x2800U, 0xbf08U, 0xee000f10U, 0xdf01U}, true);
    const auto stop = fault.cpu.Run(32);
    REQUIRE(stop.reason == RunStopReason::unsupported_instruction);
    CHECK(stop.pc == fault.code.Add(6)); // Unaligned Thumb-2, crosses a word boundary.
    CHECK(stop.instruction == 0xee000f10U);
    CHECK((fault.cpu.GetState().Cpsr() & 0x0600fc00U) == 0x800U);
    CHECK(fault.cpu.Run(32).reason == RunStopReason::unsupported_instruction);
}

TEST_CASE("Dynarmic BND-44 TLS remains supported and UDF remains distinct") {
    using namespace ogplay::cpu;
    CoprocessorGuest tls({0xee1d2f70U, 0xdf01U}, true);
    CHECK(tls.cpu.Run(32).reason == RunStopReason::supervisor_call);
    CHECK(tls.cpu.GetState().Register(CoreRegister::r2) == 0x56789000U);
    for (bool thumb : {false, true}) {
        const auto opcode = thumb ? 0xde00U : 0xe7f000f0U;
        CoprocessorGuest udf({opcode}, thumb);
        const auto stop = udf.cpu.Run(32);
        CHECK(stop.reason == RunStopReason::undefined_instruction);
        CHECK(stop.pc == udf.code);
        CHECK(stop.instruction == opcode);
    }
}

TEST_CASE("Dynarmic cached executable mapping follows successful memory syscalls") {
    using namespace ogplay;
    memory::AddressSpace space;
    memory::CheckedMemoryBus bus(space);
    const memory::GuestAddress code{0x10000}, trap{0x11000};
    const auto rw = memory::PageProtection::read | memory::PageProtection::write;
    const auto rx = memory::PageProtection::read | memory::PageProtection::execute;
    space.Map({code, 4096}, rw);
    space.Map({trap, 4096}, rw);
    bus.Write32(code, 0xe3a00001U);
    bus.Write32(code.Add(4), 0xef000001U);
    bus.Write32(trap, 0xef000000U);
    space.Protect({code, 4096}, rx);
    space.Protect({trap, 4096}, rx);
    cpu::DynarmicCpu executor(bus);
    cpu::A32State initial;
    initial.SetRegister(cpu::CoreRegister::pc, code.Value());
    executor.SetState(initial);
    REQUIRE(executor.Run(8).reason == cpu::RunStopReason::supervisor_call);
    REQUIRE(executor.GetState().Register(cpu::CoreRegister::r0) == 1);
    core::CapabilityLedger ledger;
    auto dispatcher = runtime::CreateAndroidArmSyscallDispatcher(ledger);
    runtime::BindAndroidMemorySyscalls(dispatcher, space);
    auto syscall = [&](std::uint32_t number, std::array<std::uint32_t, 6> args) {
        auto state = initial;
        state.SetRegister(cpu::CoreRegister::pc, trap.Value());
        state.SetRegister(cpu::CoreRegister::r7, number);
        for (unsigned i = 0; i < args.size(); ++i)
            state.SetRegister(static_cast<cpu::CoreRegister>(i), args[i]);
        executor.SetState(state);
        const auto stop = executor.Run(8);
        REQUIRE(stop.reason == cpu::RunStopReason::supervisor_call);
        auto outcome = runtime::DispatchAndroidArmSupervisorCall(executor, stop, dispatcher);
        REQUIRE(outcome.has_value());
        return outcome->return_value;
    };
    SUBCASE("mprotect removes execution permission from an already compiled block") {
        REQUIRE(syscall(125, {code.Value(), 4096, 0, 0, 0, 0}) == 0);
        executor.SetState(initial);
        const auto stopped = executor.Run(8);
        CHECK(stopped.reason == cpu::RunStopReason::memory_fault);
        if (stopped.fault) CHECK(stopped.fault->access == memory::AccessType::execute);
    }
    SUBCASE("munmap mmap address reuse must execute the new mapping") {
        REQUIRE(syscall(91, {code.Value(), 4096, 0, 0, 0, 0}) == 0);
        REQUIRE(syscall(192, {code.Value(), 4096, 3, 0x32, 0xffffffffU, 0}) == code.Value());
        bus.Write32(code, 0xe3a00002U);
        bus.Write32(code.Add(4), 0xef000001U);
        REQUIRE(syscall(125, {code.Value(), 4096, 5, 0, 0, 0}) == 0);
        executor.SetState(initial);
        REQUIRE(executor.Run(8).reason == cpu::RunStopReason::supervisor_call);
        CHECK(executor.GetState().Register(cpu::CoreRegister::r0) == 2);
        executor.InvalidateCodeRange({code, 4096});
        executor.SetState(initial);
        REQUIRE(executor.Run(8).reason == cpu::RunStopReason::supervisor_call);
        CHECK(executor.GetState().Register(cpu::CoreRegister::r0) == 2);
    }
}

TEST_CASE("Dynarmic mapping changes invalidate all subscribers including snapshot restore") {
    using namespace ogplay;
    memory::AddressSpace space;
    memory::CheckedMemoryBus bus(space);
    const memory::GuestAddress code{0x10000};
    const auto rw = memory::PageProtection::read | memory::PageProtection::write;
    const auto rx = memory::PageProtection::read | memory::PageProtection::execute;
    space.Map({code, 4096}, rw);
    bus.Write32(code, 0xe3a00001U); // mov r0,#1
    bus.Write32(code.Add(4), 0xef000001U);
    space.Protect({code, 4096}, rx);
    const auto snapshot = space.CaptureSnapshot();
    cpu::DynarmicCpu first(bus), peer(bus); // Even independent JIT contexts subscribe.
    cpu::A32State initial;
    initial.SetRegister(cpu::CoreRegister::pc, code.Value());
    const auto execute = [&](cpu::DynarmicCpu& executor, std::uint32_t expected) {
        executor.SetState(initial);
        REQUIRE(executor.Run(8).reason == cpu::RunStopReason::supervisor_call);
        CHECK(executor.GetState().Register(cpu::CoreRegister::r0) == expected);
    };
    execute(first, 1); execute(peer, 1);
    space.ReplaceAnonymous({code, 4096}, rw);
    bus.Write32(code, 0xe3a00002U);
    bus.Write32(code.Add(4), 0xef000001U);
    space.Protect({code, 4096}, rx);
    execute(first, 2); execute(peer, 2);
    space.RestoreSnapshot(snapshot);
    execute(first, 1); execute(peer, 1);
    { cpu::DynarmicCpu temporary(bus); execute(temporary, 1); }
    space.Unmap({code, 4096}); // Retired CPU callback must not run.
    peer.SetState(initial);
    const auto stopped = peer.Run(8);
    CHECK(stopped.reason == cpu::RunStopReason::memory_fault);
    REQUIRE(stopped.fault.has_value());
    CHECK(stopped.fault->reason == memory::FaultReason::unmapped);
}
