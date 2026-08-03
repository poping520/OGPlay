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
