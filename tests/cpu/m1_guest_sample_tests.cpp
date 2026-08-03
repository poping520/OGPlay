#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay_m1_guest/sample.h"

namespace {

namespace sample = ogplay::samples::m1_guest;

class RecordingObserver final : public ogplay::memory::MemoryAccessObserver {
public:
    void OnMemoryAccess(const ogplay::memory::BusAccess& access) override {
        accesses.push_back(access);
    }

    std::vector<ogplay::memory::BusAccess> accesses;
};

class M1GuestFixture final {
public:
    M1GuestFixture() : bus(memory, &observer), cpu(bus) {
        memory.Map({CodeAddress(), memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
        memory.Map({MailboxAddress(), memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
    }

    void LoadA32() {
        auto address = CodeAddress();
        for (const auto instruction : sample::kA32Program) {
            bus.Write32(address, instruction);
            address = address.Add(4);
        }
        ProtectCode();
    }

    void LoadThumb() {
        auto address = CodeAddress();
        for (const auto instruction : sample::kThumbProgram) {
            bus.Write16(address, instruction);
            address = address.Add(2);
        }
        ProtectCode();
    }

    void Start(const ogplay::cpu::ExecutionState execution_state,
               const std::uint64_t thread_id) {
        ogplay::cpu::A32State state;
        state.SetRegister(ogplay::cpu::CoreRegister::pc, sample::kCodeAddress);
        state.SetRegister(ogplay::cpu::CoreRegister::r4, sample::kMailboxAddress);
        state.SetState(execution_state);
        state.SetThreadId(thread_id);
        cpu.SetState(state);
    }

    void SetMailbox(const std::uint32_t input, const std::uint32_t sequence) {
        bus.Write32(MailboxAddress().Add(sample::kInputOffset), input);
        bus.Write32(MailboxAddress().Add(sample::kOutputOffset), 0);
        bus.Write32(MailboxAddress().Add(sample::kSequenceOffset), sequence);
        observer.accesses.clear();
    }

    [[nodiscard]] std::uint32_t Output() {
        return bus.Read32(MailboxAddress().Add(sample::kOutputOffset));
    }

    [[nodiscard]] std::uint32_t Sequence() {
        return bus.Read32(MailboxAddress().Add(sample::kSequenceOffset));
    }

    void CheckGuestAccesses(const std::uint64_t thread_id,
                            const std::size_t expected_count) const {
        REQUIRE(observer.accesses.size() == expected_count);
        CHECK(std::all_of(observer.accesses.begin(), observer.accesses.end(),
                          [thread_id](const auto& access) {
                              return access.thread_id == thread_id;
                          }));
        CHECK(std::any_of(observer.accesses.begin(), observer.accesses.end(),
                          [](const auto& access) {
                              return access.type == ogplay::memory::BusAccessType::execute;
                          }));
        CHECK(std::any_of(observer.accesses.begin(), observer.accesses.end(),
                          [](const auto& access) {
                              return access.type == ogplay::memory::BusAccessType::write;
                          }));
    }

    [[nodiscard]] static ogplay::memory::GuestAddress CodeAddress() {
        return ogplay::memory::GuestAddress{sample::kCodeAddress};
    }

    [[nodiscard]] static ogplay::memory::GuestAddress MailboxAddress() {
        return ogplay::memory::GuestAddress{sample::kMailboxAddress};
    }

    RecordingObserver observer;
    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus;
    ogplay::cpu::InterpreterCpu cpu;

private:
    void ProtectCode() {
        memory.Protect({CodeAddress(), memory.PageSize()},
                       ogplay::memory::PageProtection::read |
                           ogplay::memory::PageProtection::execute);
        observer.accesses.clear();
    }
};

}  // namespace

TEST_CASE("M1 A32 guest sample consumes input and replays from a snapshot") {
    M1GuestFixture fixture;
    fixture.LoadA32();
    fixture.SetMailbox(19, 41);
    fixture.Start(ogplay::cpu::ExecutionState::a32, 101);
    const auto memory_snapshot = fixture.memory.CaptureSnapshot();
    const auto cpu_snapshot = fixture.cpu.GetState();

    const auto first = fixture.cpu.Run(32);
    CHECK(first.reason == ogplay::cpu::RunStopReason::supervisor_call);
    CHECK(first.immediate == sample::kA32SupervisorCall);
    CHECK(first.ticks_consumed == sample::kA32Program.size());
    fixture.CheckGuestAccesses(101, sample::kA32Program.size() + 4);
    CHECK(fixture.Output() == sample::ExpectedA32Output(19));
    CHECK(fixture.Sequence() == 42);

    fixture.memory.RestoreSnapshot(memory_snapshot);
    fixture.cpu.SetState(cpu_snapshot);
    fixture.observer.accesses.clear();
    const auto replay = fixture.cpu.Run(32);
    CHECK(replay.reason == first.reason);
    CHECK(replay.immediate == first.immediate);
    CHECK(replay.ticks_consumed == first.ticks_consumed);
    CHECK(fixture.Output() == sample::ExpectedA32Output(19));
    CHECK(fixture.Sequence() == 42);
}

TEST_CASE("M1 Thumb guest sample uses the same mailbox and thread contract") {
    M1GuestFixture fixture;
    fixture.LoadThumb();
    fixture.SetMailbox(35, 8);
    fixture.Start(ogplay::cpu::ExecutionState::thumb, 202);

    const auto result = fixture.cpu.Run(32);
    CHECK(result.reason == ogplay::cpu::RunStopReason::supervisor_call);
    CHECK(result.immediate == sample::kThumbSupervisorCall);
    CHECK(result.ticks_consumed == sample::kThumbProgram.size());
    fixture.CheckGuestAccesses(202, sample::kThumbProgram.size() + 4);
    CHECK(fixture.Output() == sample::ExpectedThumbOutput(35));
    CHECK(fixture.Sequence() == 9);
}
