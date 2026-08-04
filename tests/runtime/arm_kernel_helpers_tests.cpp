#include <doctest/doctest.h>

#include <cstdint>
#include <memory>

#include "ogplay/cpu/dynarmic.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/syscall/arm_kernel_helpers.h"

namespace {

[[nodiscard]] ogplay::cpu::A32State RunHelper(
    ogplay::cpu::DynarmicCpu& cpu, const ogplay::memory::GuestAddress helper,
    ogplay::cpu::A32State state) {
    state.SetRegister(ogplay::cpu::CoreRegister::pc, helper.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::lr, 0xffff0100U);
    cpu.SetState(state);
    const auto stopped = cpu.Run(64);
    CHECK(stopped.reason == ogplay::cpu::RunStopReason::supervisor_call);
    CHECK(stopped.immediate == 1);
    return cpu.GetState();
}

}  // namespace

TEST_CASE("ARM kernel helpers provide barrier cmpxchg and get_tls") {
    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus(memory);
    ogplay::runtime::MapArmKernelHelpers(memory);
    const ogplay::memory::GuestAddress value{0x10000U};
    memory.Map({value, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    bus.Write32(value, 7);
    auto context = std::make_shared<ogplay::cpu::DynarmicExecutionContext>(1);
    ogplay::cpu::DynarmicCpu cpu(bus, context);
    ogplay::cpu::A32State state;
    state.SetThreadId(41);
    state.SetThreadPointer(ogplay::memory::GuestAddress{0x72000000U});
    state = RunHelper(cpu, ogplay::runtime::kArmKernelMemoryBarrier, state);

    state.SetRegister(ogplay::cpu::CoreRegister::r0, 7);
    state.SetRegister(ogplay::cpu::CoreRegister::r1, 9);
    state.SetRegister(ogplay::cpu::CoreRegister::r2, value.Value());
    state = RunHelper(cpu, ogplay::runtime::kArmKernelCmpxchg, state);
    CHECK(state.Register(ogplay::cpu::CoreRegister::r0) == 0);
    CHECK(bus.Read32(value) == 9);
    state.SetRegister(ogplay::cpu::CoreRegister::r0, 7);
    state = RunHelper(cpu, ogplay::runtime::kArmKernelCmpxchg, state);
    CHECK(state.Register(ogplay::cpu::CoreRegister::r0) == UINT32_MAX);
    CHECK(bus.Read32(value) == 9);

    state = RunHelper(cpu, ogplay::runtime::kArmKernelGetTls, state);
    CHECK(state.Register(ogplay::cpu::CoreRegister::r0) == 0x72000000U);
    CHECK(bus.Read32(ogplay::memory::GuestAddress{0xffff0ffcU}) == 5);
}
