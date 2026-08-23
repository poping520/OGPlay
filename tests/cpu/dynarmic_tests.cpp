#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "ogplay/cpu/dynarmic.h"
#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay_m1_guest/sample.h"

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
