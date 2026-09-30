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
    CHECK((state.Cpsr() & (1U << 29U)) != 0);
    state.SetRegister(ogplay::cpu::CoreRegister::r0, 7);
    state = RunHelper(cpu, ogplay::runtime::kArmKernelCmpxchg, state);
    CHECK(state.Register(ogplay::cpu::CoreRegister::r0) != 0);
    CHECK(bus.Read32(value) == 9);

    state = RunHelper(cpu, ogplay::runtime::kArmKernelGetTls, state);
    CHECK(state.Register(ogplay::cpu::CoreRegister::r0) == 0x72000000U);
    CHECK(bus.Read32(ogplay::memory::GuestAddress{0xffff0ffcU}) == 5);
}

TEST_CASE("ARM kernel cmpxchg failure clears carry according to its ABI") {
    using namespace ogplay;
    memory::AddressSpace space;
    memory::CheckedMemoryBus bus(space);
    runtime::MapArmKernelHelpers(space);
    const memory::GuestAddress data{0x10000};
    space.Map({data, 4096}, memory::PageProtection::read | memory::PageProtection::write);
    bus.Write32(data, 7);
    cpu::DynarmicCpu executor(bus);
    cpu::A32State state;
    state.SetRegister(cpu::CoreRegister::r0, 8);
    state.SetRegister(cpu::CoreRegister::r1, 9);
    state.SetRegister(cpu::CoreRegister::r2, data.Value());
    state = RunHelper(executor, runtime::kArmKernelCmpxchg, state);
    CHECK(state.Register(cpu::CoreRegister::r0) != 0);
    CHECK(bus.Read32(data) == 7);
    CHECK((state.Cpsr() & (1U << 29U)) == 0);
}
TEST_CASE("ARM kernel helper version five provides cmpxchg64") {
    using namespace ogplay;
    memory::AddressSpace space;
    memory::CheckedMemoryBus bus(space);
    runtime::MapArmKernelHelpers(space);
    const memory::GuestAddress data{0x10000};
    space.Map({data, 4096}, memory::PageProtection::read | memory::PageProtection::write);
    REQUIRE(bus.Read32(memory::GuestAddress{0xffff0ffcU}) >= 5);
    constexpr std::uint64_t old = 0x1020304050607080ULL;
    constexpr std::uint64_t replacement = 0x8877665544332211ULL;
    bus.Write64(data, old);
    bus.Write64(data.Add(8), replacement);
    bus.Write64(data.Add(16), old);
    cpu::DynarmicCpu executor(bus);
    cpu::A32State state;
    state.SetRegister(cpu::CoreRegister::sp, data.Add(4096).Value());
    state.SetRegister(cpu::CoreRegister::r0, data.Value());
    state.SetRegister(cpu::CoreRegister::r1, data.Add(8).Value());
    state.SetRegister(cpu::CoreRegister::r2, data.Add(16).Value());
    for (unsigned reg = 4; reg <= 12; ++reg)
        state.SetRegister(static_cast<cpu::CoreRegister>(reg), 0xabc000U + reg);
    const auto initial = state;
    state = RunHelper(executor, runtime::kArmKernelCmpxchg64, state);
    CHECK(state.Register(cpu::CoreRegister::r0) == 0);
    CHECK((state.Cpsr() & (1U << 29U)) != 0);
    CHECK(bus.Read64(data.Add(16)) == replacement);
    for (unsigned reg = 1; reg <= 13; ++reg) {
        if (reg == 3) continue;
        CHECK(state.Register(static_cast<cpu::CoreRegister>(reg)) == initial.Register(static_cast<cpu::CoreRegister>(reg)));
    }
    SUBCASE("low word mismatch") { bus.Write64(data.Add(16), old ^ 1U); }
    SUBCASE("high word mismatch") { bus.Write64(data.Add(16), old ^ (1ULL << 32)); }
    const auto before = bus.Read64(data.Add(16));
    state = RunHelper(executor, runtime::kArmKernelCmpxchg64, initial);
    CHECK(state.Register(cpu::CoreRegister::r0) != 0);
    CHECK((state.Cpsr() & (1U << 29U)) == 0);
    CHECK(bus.Read64(data.Add(16)) == before);
    CHECK(bus.Read32(runtime::kArmKernelMemoryBarrier) == 0xf57ff05fU);

}

namespace {
class RetryMemoryBus final : public ogplay::memory::MemoryBus {
public:
    explicit RetryMemoryBus(ogplay::memory::CheckedMemoryBus& memory) : bus(memory) {}
    unsigned attempts{};
    bool CompareExchange32(ogplay::memory::GuestAddress a, std::uint32_t expected,
                           std::uint32_t value, std::uint64_t t) override {
        if (++attempts == 1) return false; // Simulate one lost reservation.
        return bus.CompareExchange32(a, expected, value, t);
    }
    bool CompareExchange64(ogplay::memory::GuestAddress a, std::uint64_t expected,
                           std::uint64_t value, std::uint64_t t) override {
        if (++attempts == 1) return false;
        return bus.CompareExchange64(a, expected, value, t);
    }
    std::uint8_t Read8(ogplay::memory::GuestAddress a, std::uint64_t t) override { return bus.Read8(a,t); }
    std::uint16_t Read16(ogplay::memory::GuestAddress a, std::uint64_t t) override { return bus.Read16(a,t); }
    std::uint32_t Read32(ogplay::memory::GuestAddress a, std::uint64_t t) override { return bus.Read32(a,t); }
    std::uint64_t Read64(ogplay::memory::GuestAddress a, std::uint64_t t) override { return bus.Read64(a,t); }
    std::uint16_t Fetch16(ogplay::memory::GuestAddress a, std::uint64_t t) override { return bus.Fetch16(a,t); }
    std::uint32_t Fetch32(ogplay::memory::GuestAddress a, std::uint64_t t) override { return bus.Fetch32(a,t); }
    void Write8(ogplay::memory::GuestAddress a, std::uint8_t v, std::uint64_t t) override { bus.Write8(a,v,t); }
    void Write16(ogplay::memory::GuestAddress a, std::uint16_t v, std::uint64_t t) override { bus.Write16(a,v,t); }
    void Write32(ogplay::memory::GuestAddress a, std::uint32_t v, std::uint64_t t) override { bus.Write32(a,v,t); }
    void Write64(ogplay::memory::GuestAddress a, std::uint64_t v, std::uint64_t t) override { bus.Write64(a,v,t); }
private:
    ogplay::memory::CheckedMemoryBus& bus;
};
}

TEST_CASE("ARM kernel compare exchange retries a lost reservation") {
    using namespace ogplay;
    memory::AddressSpace space;
    memory::CheckedMemoryBus bus(space);
    runtime::MapArmKernelHelpers(space);
    const memory::GuestAddress data{0x10000};
    space.Map({data, 4096}, memory::PageProtection::read | memory::PageProtection::write);
    bus.Write64(data, 7);
    bus.Write64(data.Add(8), 9);
    bus.Write64(data.Add(16), 7);
    RetryMemoryBus retry(bus);
    cpu::DynarmicCpu executor(retry);
    cpu::A32State state;
    state.SetRegister(cpu::CoreRegister::sp, data.Add(4096).Value());
    state.SetRegister(cpu::CoreRegister::r2, data.Add(16).Value());
    SUBCASE("32 bit") {
        state.SetRegister(cpu::CoreRegister::r0, 7);
        state.SetRegister(cpu::CoreRegister::r1, 9);
        state = RunHelper(executor, runtime::kArmKernelCmpxchg, state);
    }
    SUBCASE("64 bit") {
        state.SetRegister(cpu::CoreRegister::r0, data.Value());
        state.SetRegister(cpu::CoreRegister::r1, data.Add(8).Value());
        state = RunHelper(executor, runtime::kArmKernelCmpxchg64, state);
    }
    CHECK(retry.attempts == 2);
    CHECK(state.Register(cpu::CoreRegister::r0) == 0);
    CHECK((state.Cpsr() & (1U << 29U)) != 0);
    CHECK(bus.Read64(data.Add(16)) == 9);
}
