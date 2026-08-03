#include <doctest/doctest.h>

#include <cstdint>
#include <stdexcept>

#include "ogplay/cpu/cpu.h"

TEST_CASE("A32 state owns core extended and status registers") {
    ogplay::cpu::A32State state;
    CHECK(state.Cpsr() == 0x10);
    CHECK(state.State() == ogplay::cpu::ExecutionState::a32);

    state.SetRegister(ogplay::cpu::CoreRegister::r0, 0x12345678);
    state.SetRegister(ogplay::cpu::CoreRegister::sp, 0x70000000);
    state.SetRegister(ogplay::cpu::CoreRegister::lr, 0x20001);
    state.SetRegister(ogplay::cpu::CoreRegister::pc, 0x10000);
    state.SetExtendedRegister(63, 0xabcdef01);
    state.SetFpscr(0x01000000);
    state.SetFpexc(0x40000000);
    state.SetThreadId(27);

    CHECK(state.Register(ogplay::cpu::CoreRegister::r0) == 0x12345678);
    CHECK(state.CoreRegisters()[13] == 0x70000000);
    CHECK(state.CoreRegisters()[14] == 0x20001);
    CHECK(state.CoreRegisters()[15] == 0x10000);
    CHECK(state.ExtendedRegisters()[63] == 0xabcdef01);
    CHECK(state.Fpscr() == 0x01000000);
    CHECK(state.Fpexc() == 0x40000000);
    CHECK(state.ThreadId() == 27);
    CHECK_THROWS_AS(
        static_cast<void>(state.Register(static_cast<ogplay::cpu::CoreRegister>(16))),
        std::out_of_range);
    CHECK_THROWS_AS(state.SetExtendedRegister(64, 0), std::out_of_range);
}

TEST_CASE("A32 execution state only changes the CPSR Thumb bit") {
    ogplay::cpu::A32State state;
    state.SetCpsr(0xf0000010);
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    CHECK(state.Cpsr() == 0xf0000030);
    CHECK(state.State() == ogplay::cpu::ExecutionState::thumb);
    state.SetState(ogplay::cpu::ExecutionState::a32);
    CHECK(state.Cpsr() == 0xf0000010);
}

TEST_CASE("CPU snapshot and memory fault result retain deterministic state") {
    ogplay::cpu::CpuSnapshot snapshot;
    snapshot.state.SetRegister(ogplay::cpu::CoreRegister::pc, 0x1234);
    snapshot.state.SetThreadId(9);
    const auto copy = snapshot;
    CHECK(copy.version == ogplay::cpu::kCpuSnapshotVersion);
    CHECK(copy.state == snapshot.state);

    ogplay::cpu::RunResult result;
    result.reason = ogplay::cpu::RunStopReason::memory_fault;
    result.pc = ogplay::memory::GuestAddress{0x1234};
    result.fault = ogplay::cpu::CpuFault{
        ogplay::memory::GuestAddress{0x2000},
        ogplay::memory::AccessType::write,
        ogplay::memory::FaultReason::permission_denied,
        9,
    };
    REQUIRE(result.fault.has_value());
    CHECK(result.fault->address == ogplay::memory::GuestAddress{0x2000});
    CHECK(result.fault->thread_id == 9);
}
