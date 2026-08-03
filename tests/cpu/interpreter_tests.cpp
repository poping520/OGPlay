#include <doctest/doctest.h>

#include <array>
#include <cstdint>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"

namespace {

class CpuFixture final {
public:
    CpuFixture() : bus(memory), cpu(bus) {
        memory.Map({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write |
                       ogplay::memory::PageProtection::execute);
    }

    void WriteA32(const std::initializer_list<std::uint32_t> instructions) {
        auto address = code;
        for (const auto instruction : instructions) {
            bus.Write32(address, instruction);
            address = address.Add(4);
        }
    }

    void WriteThumb(const std::initializer_list<std::uint16_t> instructions) {
        auto address = code;
        for (const auto instruction : instructions) {
            bus.Write16(address, instruction);
            address = address.Add(2);
        }
    }

    void Start(const ogplay::cpu::ExecutionState execution_state) {
        ogplay::cpu::A32State state;
        state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
        state.SetThreadId(33);
        state.SetState(execution_state);
        cpu.SetState(state);
    }

    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus;
    ogplay::cpu::InterpreterCpu cpu;
    const ogplay::memory::GuestAddress code{0x10000};
};

}  // namespace

TEST_CASE("A32 interpreter executes conditional scalar control flow") {
    CpuFixture fixture;
    fixture.WriteA32({
        0xe3a00028,  // mov r0, #40
        0xe2400001,  // sub r0, r0, #1
        0xe2800003,  // add r0, r0, #3
        0xe350002a,  // cmp r0, #42
        0x1a000000,  // bne +0 (not taken)
        0xe3a01007,  // mov r1, #7
        0xef000123,  // svc #0x123
    });
    fixture.Start(ogplay::cpu::ExecutionState::a32);

    const auto result = fixture.cpu.Run(10);
    CHECK(result.reason == ogplay::cpu::RunStopReason::supervisor_call);
    CHECK(result.ticks_consumed == 7);
    CHECK(result.pc == fixture.code.Add(24));
    CHECK(result.immediate == 0x123);
    const auto state = fixture.cpu.GetState();
    CHECK(state.Register(ogplay::cpu::CoreRegister::r0) == 42);
    CHECK(state.Register(ogplay::cpu::CoreRegister::r1) == 7);
    CHECK(state.Register(ogplay::cpu::CoreRegister::pc) == fixture.code.Value() + 28);
}

TEST_CASE("A32 BL and BX preserve the architectural return address") {
    CpuFixture fixture;
    fixture.WriteA32({
        0xeb000001,  // bl 0x1000c
        0xef000005,  // svc #5
        0xe1a00000,  // mov r0, r0
        0xe3a02009,  // mov r2, #9
        0xe12fff1e,  // bx lr
    });
    fixture.Start(ogplay::cpu::ExecutionState::a32);

    const auto result = fixture.cpu.Run(8);
    CHECK(result.reason == ogplay::cpu::RunStopReason::supervisor_call);
    CHECK(result.ticks_consumed == 4);
    const auto state = fixture.cpu.GetState();
    CHECK(state.Register(ogplay::cpu::CoreRegister::r2) == 9);
    CHECK(state.Register(ogplay::cpu::CoreRegister::lr) == fixture.code.Value() + 4);
}

TEST_CASE("Thumb interpreter executes immediate arithmetic and conditional branch") {
    CpuFixture fixture;
    fixture.WriteThumb({
        0x2028,  // movs r0, #40
        0x3801,  // subs r0, #1
        0x3003,  // adds r0, #3
        0x282a,  // cmp r0, #42
        0xd100,  // bne +0 (not taken)
        0x2107,  // movs r1, #7
        0xdf05,  // svc #5
    });
    fixture.Start(ogplay::cpu::ExecutionState::thumb);

    const auto result = fixture.cpu.Run(10);
    CHECK(result.reason == ogplay::cpu::RunStopReason::supervisor_call);
    CHECK(result.ticks_consumed == 7);
    CHECK(result.immediate == 5);
    const auto state = fixture.cpu.GetState();
    CHECK(state.Register(ogplay::cpu::CoreRegister::r0) == 42);
    CHECK(state.Register(ogplay::cpu::CoreRegister::r1) == 7);
    CHECK(state.State() == ogplay::cpu::ExecutionState::thumb);
}

TEST_CASE("Thumb BX switches execution state before the next fetch") {
    CpuFixture fixture;
    fixture.WriteThumb({0x4700, 0xbf00});  // bx r0; nop
    fixture.bus.Write32(fixture.code.Add(4), 0xef000003);  // svc #3
    fixture.Start(ogplay::cpu::ExecutionState::thumb);
    auto state = fixture.cpu.GetState();
    state.SetRegister(ogplay::cpu::CoreRegister::r0, fixture.code.Value() + 4);
    fixture.cpu.SetState(state);

    const auto result = fixture.cpu.Run(4);
    CHECK(result.reason == ogplay::cpu::RunStopReason::supervisor_call);
    CHECK(result.ticks_consumed == 2);
    CHECK(result.immediate == 3);
    CHECK(fixture.cpu.GetState().State() == ogplay::cpu::ExecutionState::a32);
}

TEST_CASE("A32 interpreter loads stores bytes words and applies writeback") {
    CpuFixture fixture;
    const ogplay::memory::GuestAddress data{0x20000};
    fixture.memory.Map({data, fixture.memory.PageSize()},
                       ogplay::memory::PageProtection::read |
                           ogplay::memory::PageProtection::write);
    fixture.WriteA32({
        0xe5a01004,  // str r1, [r0, #4]!
        0xe5902000,  // ldr r2, [r0]
        0xe5c01005,  // strb r1, [r0, #5]
        0xe5d03005,  // ldrb r3, [r0, #5]
        0xef000001,  // svc #1
    });
    fixture.Start(ogplay::cpu::ExecutionState::a32);
    auto state = fixture.cpu.GetState();
    state.SetRegister(ogplay::cpu::CoreRegister::r0, data.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r1, 0x1234562a);
    fixture.cpu.SetState(state);

    const auto result = fixture.cpu.Run(8);
    CHECK(result.reason == ogplay::cpu::RunStopReason::supervisor_call);
    CHECK(result.ticks_consumed == 5);
    state = fixture.cpu.GetState();
    CHECK(state.Register(ogplay::cpu::CoreRegister::r0) == data.Value() + 4);
    CHECK(state.Register(ogplay::cpu::CoreRegister::r2) == 0x1234562a);
    CHECK(state.Register(ogplay::cpu::CoreRegister::r3) == 0x2a);
    CHECK(fixture.bus.Read32(data.Add(4)) == 0x1234562a);
    CHECK(fixture.bus.Read8(data.Add(9)) == 0x2a);
}

TEST_CASE("Thumb interpreter loads and stores immediate byte and word operands") {
    CpuFixture fixture;
    const ogplay::memory::GuestAddress data{0x20000};
    fixture.memory.Map({data, fixture.memory.PageSize()},
                       ogplay::memory::PageProtection::read |
                           ogplay::memory::PageProtection::write);
    fixture.WriteThumb({
        0x6041,  // str r1, [r0, #4]
        0x6842,  // ldr r2, [r0, #4]
        0x7041,  // strb r1, [r0, #1]
        0x7843,  // ldrb r3, [r0, #1]
        0x4c00,  // ldr r4, [pc, #0]
        0xdf02,  // svc #2
    });
    fixture.bus.Write32(fixture.code.Add(12), 0x76543210);
    fixture.Start(ogplay::cpu::ExecutionState::thumb);
    auto state = fixture.cpu.GetState();
    state.SetRegister(ogplay::cpu::CoreRegister::r0, data.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r1, 0xabcdef2a);
    fixture.cpu.SetState(state);

    const auto result = fixture.cpu.Run(8);
    CHECK(result.reason == ogplay::cpu::RunStopReason::supervisor_call);
    CHECK(result.ticks_consumed == 6);
    state = fixture.cpu.GetState();
    CHECK(state.Register(ogplay::cpu::CoreRegister::r2) == 0xabcdef2a);
    CHECK(state.Register(ogplay::cpu::CoreRegister::r3) == 0x2a);
    CHECK(state.Register(ogplay::cpu::CoreRegister::r4) == 0x76543210);
}

TEST_CASE("interpreter data fault leaves instruction state and destination unchanged") {
    CpuFixture fixture;
    const ogplay::memory::GuestAddress data{0x20000};
    fixture.memory.Map({data, fixture.memory.PageSize()},
                       ogplay::memory::PageProtection::read);
    fixture.WriteA32({0xe5801000});  // str r1, [r0]
    fixture.Start(ogplay::cpu::ExecutionState::a32);
    auto state = fixture.cpu.GetState();
    state.SetRegister(ogplay::cpu::CoreRegister::r0, data.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r1, 0xfeedface);
    fixture.cpu.SetState(state);

    const auto result = fixture.cpu.Run(4);
    CHECK(result.reason == ogplay::cpu::RunStopReason::memory_fault);
    CHECK(result.ticks_consumed == 0);
    REQUIRE(result.fault.has_value());
    CHECK(result.fault->address == data);
    CHECK(result.fault->access == ogplay::memory::AccessType::write);
    state = fixture.cpu.GetState();
    CHECK(state.Register(ogplay::cpu::CoreRegister::pc) == fixture.code.Value());
    CHECK(state.Register(ogplay::cpu::CoreRegister::r0) == data.Value());
    CHECK(fixture.bus.Read32(data) == 0);
}

TEST_CASE("interpreter reports budgets halt undefined instructions and fetch faults") {
    SUBCASE("budget and halt") {
        CpuFixture fixture;
        fixture.WriteA32({0xe1a00000, 0xe1a00000});
        fixture.Start(ogplay::cpu::ExecutionState::a32);
        const auto budget = fixture.cpu.Run(1);
        CHECK(budget.reason == ogplay::cpu::RunStopReason::budget_exhausted);
        CHECK(budget.ticks_consumed == 1);
        fixture.cpu.RequestHalt();
        const auto halt = fixture.cpu.Run(10);
        CHECK(halt.reason == ogplay::cpu::RunStopReason::halt_requested);
        CHECK(halt.ticks_consumed == 0);
    }
    SUBCASE("undefined instruction") {
        CpuFixture fixture;
        fixture.WriteA32({0xe7f000f0});
        fixture.Start(ogplay::cpu::ExecutionState::a32);
        const auto result = fixture.cpu.Run(10);
        CHECK(result.reason == ogplay::cpu::RunStopReason::undefined_instruction);
        CHECK(result.ticks_consumed == 1);
        CHECK(fixture.cpu.GetState().Register(ogplay::cpu::CoreRegister::pc) ==
              fixture.code.Value());
    }
    SUBCASE("undefined Thumb-32 instruction") {
        CpuFixture fixture;
        fixture.WriteThumb({0xf000, 0x8000});
        fixture.Start(ogplay::cpu::ExecutionState::thumb);
        const auto result = fixture.cpu.Run(10);
        CHECK(result.reason == ogplay::cpu::RunStopReason::undefined_instruction);
        CHECK(result.instruction == 0x8000f000);
        CHECK(fixture.cpu.GetState().Register(ogplay::cpu::CoreRegister::pc) ==
              fixture.code.Value());
    }
    SUBCASE("Thumb breakpoint") {
        CpuFixture fixture;
        fixture.WriteThumb({0xbe2a});
        fixture.Start(ogplay::cpu::ExecutionState::thumb);
        const auto result = fixture.cpu.Run(10);
        CHECK(result.reason == ogplay::cpu::RunStopReason::breakpoint);
        CHECK(result.immediate == 0x2a);
        CHECK(fixture.cpu.GetState().Register(ogplay::cpu::CoreRegister::pc) ==
              fixture.code.Value() + 2);
    }
    SUBCASE("execute permission fault") {
        CpuFixture fixture;
        fixture.WriteA32({0xe1a00000});
        fixture.memory.Protect({fixture.code, fixture.memory.PageSize()},
                               ogplay::memory::PageProtection::read);
        fixture.Start(ogplay::cpu::ExecutionState::a32);
        const auto result = fixture.cpu.Run(10);
        CHECK(result.reason == ogplay::cpu::RunStopReason::memory_fault);
        CHECK(result.ticks_consumed == 0);
        REQUIRE(result.fault.has_value());
        CHECK(result.fault->access == ogplay::memory::AccessType::execute);
        CHECK(result.fault->thread_id == 33);
    }
}
