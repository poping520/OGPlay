#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/bus.h"
#include "ogplay/session/profile_native_execution.h"

namespace {

struct ExecutionFixture final {
    ExecutionFixture() : bus(memory), cpu(bus) {
        memory.Map({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
        memory.Map({stack, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
    }

    void Start() {
        memory.Protect({code, memory.PageSize()},
                       ogplay::memory::PageProtection::read |
                           ogplay::memory::PageProtection::execute);
        ogplay::cpu::A32State state;
        state.SetThreadId(thread_id);
        cpu.SetState(state);
        lifecycle.Register(thread_id);
    }

    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus;
    ogplay::cpu::InterpreterCpu cpu;
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::A32SyscallDispatcher dispatcher{
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger)};
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    const ogplay::memory::GuestAddress code{0x10000U};
    const ogplay::memory::GuestAddress stack{0x20000U};
    const std::uint64_t thread_id{91};
};

}  // namespace

TEST_CASE("Profile native execution preserves ordered invocation results") {
    ExecutionFixture fixture;
    fixture.bus.Write32(fixture.code, 0xe0800001U);         // add r0, r0, r1
    fixture.bus.Write32(fixture.code.Add(4), 0xe12fff1eU);  // bx lr
    const auto second = fixture.code.Add(16);
    fixture.bus.Write32(second, 0xe59d0000U);               // ldr r0, [sp]
    fixture.bus.Write32(second.Add(4), 0xe12fff1eU);        // bx lr
    const auto return_trap = fixture.code.Add(64);
    fixture.bus.Write32(return_trap, 0xef000001U);          // svc #1
    fixture.Start();

    const std::vector<ogplay::session::ProfileNativeInvocation> invocations{
        {1, "Java_sample_First", fixture.code, {4U, 5U, 0U, 0U}, {}},
        {3, "Java_sample_Second", second, {},
         std::vector<std::uint32_t>{17U, 0U}}};
    const auto results =
        ogplay::session::ExecuteProfileNativeInvocations(
            invocations, fixture.cpu, fixture.dispatcher,
            fixture.lifecycle, fixture.memory,
            fixture.stack.Add(fixture.memory.PageSize()), return_trap, 16);

    REQUIRE(results.size() == 2);
    CHECK(results[0].call_index == 1);
    CHECK(results[0].export_name == "Java_sample_First");
    CHECK(results[0].guest.return_value == 9U);
    CHECK(results[1].call_index == 3);
    CHECK(results[1].guest.return_value == 17U);
}

TEST_CASE("Profile native execution preflights the full batch") {
    ExecutionFixture fixture;
    fixture.bus.Write32(fixture.code, 0xef000002U);         // svc #2
    fixture.bus.Write32(fixture.code.Add(4), 0xe12fff1eU);  // bx lr
    const auto return_trap = fixture.code.Add(64);
    fixture.bus.Write32(return_trap, 0xef000001U);
    fixture.Start();
    const std::vector<ogplay::session::ProfileNativeInvocation> invocations{
        {0, "Java_sample_First", fixture.code, {}, {}},
        {1, "Java_sample_Bad", fixture.code, {},
         std::vector<std::uint32_t>{1U}}};
    std::uint32_t hle_calls{};

    CHECK_THROWS_WITH_AS(
        static_cast<void>(
            ogplay::session::ExecuteProfileNativeInvocations(
                invocations, fixture.cpu, fixture.dispatcher,
                fixture.lifecycle, fixture.memory,
                fixture.stack.Add(fixture.memory.PageSize()), return_trap,
                16,
                [&hle_calls](ogplay::cpu::Cpu&,
                             const ogplay::cpu::RunResult&) {
                    ++hle_calls;
                    return true;
                })),
        "profile native execution contains an incomplete invocation",
        ogplay::session::ProfileNativeExecutionError);
    CHECK(hle_calls == 0U);
}

TEST_CASE("Profile native execution identifies the failing export") {
    ExecutionFixture fixture;
    fixture.bus.Write32(fixture.code, 0xef000004U);  // unhandled svc #4
    const auto return_trap = fixture.code.Add(64);
    fixture.bus.Write32(return_trap, 0xef000001U);
    fixture.Start();
    const std::array<ogplay::session::ProfileNativeInvocation, 1> invocations{{
        {7, "Java_sample_Failing", fixture.code, {}, {}},
    }};

    CHECK_THROWS_WITH_AS(
        static_cast<void>(
            ogplay::session::ExecuteProfileNativeInvocations(
                invocations, fixture.cpu, fixture.dispatcher,
                fixture.lifecycle, fixture.memory,
                fixture.stack.Add(fixture.memory.PageSize()), return_trap,
                8)),
        doctest::Contains(
            "profile native call 7 (Java_sample_Failing) failed: "
            "A32 guest call stopped outside a handled boundary"),
        ogplay::session::ProfileNativeExecutionError);
}
