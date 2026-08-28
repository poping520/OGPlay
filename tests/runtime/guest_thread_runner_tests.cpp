#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/execution/guest_thread_runner.h"

namespace {

struct RunnerFixture final {
    RunnerFixture() : bus(memory), cpu(bus) {
        memory.Map({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
        memory.Map({stack, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
    }

    void Start(const std::uint64_t thread_id) {
        memory.Protect({code, memory.PageSize()},
                       ogplay::memory::PageProtection::read |
                           ogplay::memory::PageProtection::execute);
        ogplay::cpu::A32State state;
        state.SetRegister(ogplay::cpu::CoreRegister::pc, code.Value());
        state.SetThreadId(thread_id);
        cpu.SetState(state);
        lifecycle.Register(thread_id);
    }

    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus;
    ogplay::cpu::InterpreterCpu cpu;
    ogplay::cpu::FutexTable futex;
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::A32SyscallDispatcher dispatcher{
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger)};
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    const ogplay::memory::GuestAddress code{0x10000U};
    const ogplay::memory::GuestAddress stack{0x20000U};
};

class SlicedCallCpu final : public ogplay::cpu::Cpu {
public:
    explicit SlicedCallCpu(const ogplay::memory::GuestAddress return_trap)
        : return_trap_(return_trap) {}

    ogplay::cpu::RunResult Run(const std::uint64_t tick_budget) override {
        budgets.push_back(tick_budget);
        if (budgets.size() <= 2U) {
            return {tick_budget, ogplay::cpu::RunStopReason::budget_exhausted,
                    ogplay::memory::GuestAddress{0U}, 0U, 0U, std::nullopt};
        }
        state_.SetRegister(ogplay::cpu::CoreRegister::r0, 73U);
        return {1U, ogplay::cpu::RunStopReason::supervisor_call,
                return_trap_, 0U, 1U, std::nullopt};
    }
    ogplay::cpu::A32State GetState() const override { return state_; }
    void SetState(const ogplay::cpu::A32State& state) override {
        state_ = state;
    }
    void RequestHalt() noexcept override {}

    std::vector<std::uint64_t> budgets;

private:
    ogplay::memory::GuestAddress return_trap_;
    ogplay::cpu::A32State state_;
};

class SupervisorLoopCpu final : public ogplay::cpu::Cpu {
public:
    explicit SupervisorLoopCpu(
        std::uint32_t immediate = 0U,
        std::optional<std::uint32_t> repeated_r0 = std::nullopt)
        : immediate_(immediate), repeated_r0_(repeated_r0) {}

    ogplay::cpu::RunResult Run(std::uint64_t tick_budget) override {
        budgets.push_back(tick_budget);
        if (repeated_r0_.has_value()) {
            state_.SetRegister(ogplay::cpu::CoreRegister::r0, *repeated_r0_);
        }
        return {1U, ogplay::cpu::RunStopReason::supervisor_call,
                ogplay::memory::GuestAddress{0x12000U}, 0U, immediate_,
                std::nullopt};
    }
    ogplay::cpu::A32State GetState() const override { return state_; }
    void SetState(const ogplay::cpu::A32State& state) override {
        state_ = state;
    }
    void RequestHalt() noexcept override {}

    std::vector<std::uint64_t> budgets;

private:
    std::uint32_t immediate_{};
    std::optional<std::uint32_t> repeated_r0_;
    ogplay::cpu::A32State state_;
};

class BoundaryRefreshingCallCpu final : public ogplay::cpu::Cpu {
public:
    explicit BoundaryRefreshingCallCpu(
        const ogplay::memory::GuestAddress return_trap,
        const std::uint32_t boundary_immediate = 2U,
        const std::optional<std::uint32_t> repeated_r0 = std::nullopt)
        : return_trap_(return_trap),
          boundary_immediate_(boundary_immediate),
          repeated_r0_(repeated_r0) {}

    ogplay::cpu::RunResult Run(const std::uint64_t tick_budget) override {
        budgets.push_back(tick_budget);
        if (budgets.size() <= 2U) {
            if (repeated_r0_.has_value()) {
                state_.SetRegister(ogplay::cpu::CoreRegister::r0,
                                   *repeated_r0_);
            }
            return {tick_budget,
                    ogplay::cpu::RunStopReason::supervisor_call,
                    ogplay::memory::GuestAddress{0x12000U}, 0U,
                    boundary_immediate_, std::nullopt};
        }
        state_.SetRegister(ogplay::cpu::CoreRegister::r0, 91U);
        return {tick_budget,
                ogplay::cpu::RunStopReason::supervisor_call,
                return_trap_, 0U, 1U, std::nullopt};
    }
    ogplay::cpu::A32State GetState() const override { return state_; }
    void SetState(const ogplay::cpu::A32State& state) override {
        state_ = state;
    }
    void RequestHalt() noexcept override {}

    std::vector<std::uint64_t> budgets;

private:
    ogplay::memory::GuestAddress return_trap_;
    std::uint32_t boundary_immediate_{};
    std::optional<std::uint32_t> repeated_r0_;
    ogplay::cpu::A32State state_;
};

}  // namespace

TEST_CASE("JNI reentry is the explicit advanced boundary signal") {
    CHECK(ogplay::runtime::ClassifyJniReentry(true) ==
          ogplay::runtime::SupervisorCallProgress::handled_advanced);
    CHECK(ogplay::runtime::ClassifyJniReentry(false) ==
          ogplay::runtime::SupervisorCallProgress::not_handled);
}

TEST_CASE("guest thread runner consumes SVC exit through lifecycle") {
    RunnerFixture fixture;
    fixture.bus.Write32(fixture.code, 0xe3a07001U);         // mov r7, #1
    fixture.bus.Write32(fixture.code.Add(4), 0xe3a00007U);  // mov r0, #7
    fixture.bus.Write32(fixture.code.Add(8), 0xef000000U);  // svc #0
    fixture.Start(77);
    ogplay::runtime::BindAndroidThreadLifecycleSyscalls(
        fixture.dispatcher, fixture.lifecycle);
    const auto outcome = ogplay::runtime::RunAndroidArmGuestThread(
        fixture.cpu, fixture.dispatcher, fixture.lifecycle, fixture.bus,
        fixture.futex, 16);
    CHECK(outcome.reason == ogplay::runtime::GuestThreadRunStop::guest_exit);
    REQUIRE(outcome.exit.has_value());
    CHECK(outcome.exit->state.exit_code == 7);
    CHECK(outcome.exit->state.status ==
          ogplay::runtime::GuestThreadStatus::exited);
    CHECK(outcome.ticks_consumed == 3);
}

TEST_CASE("guest thread runner preserves unhandled traps and budgets") {
    SUBCASE("non-Linux SVC") {
        RunnerFixture fixture;
        fixture.bus.Write32(fixture.code, 0xef000001U);
        fixture.Start(81);
        const auto outcome = ogplay::runtime::RunAndroidArmGuestThread(
            fixture.cpu, fixture.dispatcher, fixture.lifecycle, fixture.bus,
            fixture.futex, 4);
        CHECK(outcome.reason ==
              ogplay::runtime::GuestThreadRunStop::unhandled_supervisor_call);
    }
    SUBCASE("budget") {
        RunnerFixture fixture;
        fixture.bus.Write32(fixture.code, 0xeafffffeU);  // b .
        fixture.Start(82);
        const auto outcome = ogplay::runtime::RunAndroidArmGuestThread(
            fixture.cpu, fixture.dispatcher, fixture.lifecycle, fixture.bus,
            fixture.futex, 5);
        CHECK(outcome.reason ==
              ogplay::runtime::GuestThreadRunStop::budget_exhausted);
        CHECK(outcome.ticks_consumed == 5);
    }
}

TEST_CASE("guest thread runner commits set_tls to the active CPU") {
    RunnerFixture fixture;
    fixture.bus.Write32(fixture.code, 0xef000000U);         // svc #0: set_tls
    fixture.bus.Write32(fixture.code.Add(4), 0xef000001U);  // svc #1: stop
    fixture.Start(83);
    auto state = fixture.cpu.GetState();
    state.SetRegister(ogplay::cpu::CoreRegister::r7, 0x0f0005U);
    state.SetRegister(ogplay::cpu::CoreRegister::r0, 0x72000000U);
    fixture.cpu.SetState(state);
    ogplay::runtime::BindAndroidArmPrivateSyscalls(
        fixture.dispatcher, fixture.memory,
        [&fixture](const std::uint64_t thread_id,
                   const ogplay::memory::GuestAddress pointer) {
            fixture.lifecycle.SetThreadPointer(thread_id, pointer);
            return true;
        });

    const auto outcome = ogplay::runtime::RunAndroidArmGuestThread(
        fixture.cpu, fixture.dispatcher, fixture.lifecycle, fixture.bus,
        fixture.futex, 8);
    CHECK(outcome.reason ==
          ogplay::runtime::GuestThreadRunStop::unhandled_supervisor_call);
    CHECK(fixture.cpu.GetState().ThreadPointer() ==
          ogplay::memory::GuestAddress{0x72000000U});
}

TEST_CASE("guest thread runner consumes only explicitly handled HLE traps") {
    RunnerFixture fixture;
    fixture.bus.Write32(fixture.code, 0xef000002U);         // svc #2: HLE
    fixture.bus.Write32(fixture.code.Add(4), 0xe3a07001U);  // mov r7, #1
    fixture.bus.Write32(fixture.code.Add(8), 0xef000000U);  // svc #0: exit
    fixture.Start(84);
    ogplay::runtime::BindAndroidThreadLifecycleSyscalls(
        fixture.dispatcher, fixture.lifecycle);
    std::uint32_t calls{};
    const auto outcome = ogplay::runtime::RunAndroidArmGuestThread(
        fixture.cpu, fixture.dispatcher, fixture.lifecycle, fixture.bus,
        fixture.futex, 16,
        [&calls](ogplay::cpu::Cpu& cpu,
                 const ogplay::cpu::RunResult& stopped) {
            if (stopped.immediate != 2) {
                return ogplay::runtime::SupervisorCallProgress::not_handled;
            }
            ++calls;
            auto state = cpu.GetState();
            state.SetRegister(ogplay::cpu::CoreRegister::r0, 9);
            cpu.SetState(state);
            return ogplay::runtime::SupervisorCallProgress::handled_idle;
        });
    CHECK(calls == 1);
    CHECK(outcome.reason == ogplay::runtime::GuestThreadRunStop::guest_exit);
    REQUIRE(outcome.exit.has_value());
    CHECK(outcome.exit->state.exit_code == 9);
}

TEST_CASE("A32 guest call executes registers and aligned stack words") {
    RunnerFixture fixture;
    fixture.bus.Write32(fixture.code, 0xe0800001U);         // add r0, r0, r1
    fixture.bus.Write32(fixture.code.Add(4), 0xe0800002U);  // add r0, r0, r2
    fixture.bus.Write32(fixture.code.Add(8), 0xe0800003U);  // add r0, r0, r3
    fixture.bus.Write32(fixture.code.Add(12), 0xe59d4000U); // ldr r4, [sp]
    fixture.bus.Write32(fixture.code.Add(16), 0xe0800004U); // add r0, r0, r4
    fixture.bus.Write32(fixture.code.Add(20), 0xe3a0102aU); // mov r1, #42
    fixture.bus.Write32(fixture.code.Add(24), 0xe12fff1eU); // bx lr
    const auto return_trap = fixture.code.Add(64);
    fixture.bus.Write32(return_trap, 0xef000001U);          // svc #1
    fixture.Start(85);

    const std::array<std::uint32_t, 2> stack{5U, 0U};
    const ogplay::runtime::A32GuestCallFrame frame{
        fixture.code, {1U, 2U, 3U, 4U}, stack};
    const auto result = ogplay::runtime::InvokeA32GuestCall(
        fixture.cpu, fixture.dispatcher, fixture.lifecycle, fixture.memory,
        frame, fixture.stack.Add(fixture.memory.PageSize()), return_trap, 32);

    CHECK(result.return_value == 15U);
    CHECK(result.return_value_high == 42U);
    CHECK(result.ticks_consumed == 8U);
    const auto state = fixture.cpu.GetState();
    CHECK(state.Register(ogplay::cpu::CoreRegister::sp) ==
          fixture.stack.Add(fixture.memory.PageSize() - 8U).Value());
    CHECK(fixture.bus.Read32(
              ogplay::memory::GuestAddress{
                  state.Register(ogplay::cpu::CoreRegister::sp)},
              85) == 5U);
}

TEST_CASE("A32 guest call slices long execution and publishes progress") {
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress stack{0x20000U};
    memory.Map({stack, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    const ogplay::memory::GuestAddress return_trap{0x30000U};
    SlicedCallCpu cpu{return_trap};
    ogplay::cpu::A32State state;
    state.SetThreadId(91U);
    cpu.SetState(state);
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    lifecycle.Register(91U);
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher = ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    std::vector<std::uint64_t> observations;

    const auto result = ogplay::runtime::InvokeA32GuestCall(
        cpu, dispatcher, lifecycle, memory,
        {ogplay::memory::GuestAddress{0x10000U}, {}, {}},
        stack.Add(memory.PageSize()), return_trap,
        ogplay::runtime::kA32GuestCallSliceTicks * 2U + 2U, {},
        [&observations](const std::uint64_t consumed_ticks) {
            observations.push_back(consumed_ticks);
        });

    CHECK(result.return_value == 73U);
    CHECK(result.ticks_consumed ==
          ogplay::runtime::kA32GuestCallSliceTicks * 2U + 1U);
    CHECK(cpu.budgets == std::vector<std::uint64_t>{
                             ogplay::runtime::kA32GuestCallSliceTicks,
                             ogplay::runtime::kA32GuestCallSliceTicks, 2U});
    CHECK(observations == std::vector<std::uint64_t>{
                              ogplay::runtime::kA32GuestCallSliceTicks,
                              ogplay::runtime::kA32GuestCallSliceTicks * 2U});
}

TEST_CASE("A32 guest call consumes only explicit HLE traps") {
    RunnerFixture fixture;
    fixture.bus.Write32(fixture.code, 0xef000002U);         // svc #2
    fixture.bus.Write32(fixture.code.Add(4), 0xe12fff1eU);  // bx lr
    const auto return_trap = fixture.code.Add(64);
    fixture.bus.Write32(return_trap, 0xef000001U);          // svc #1
    fixture.Start(86);

    std::uint32_t calls{};
    const ogplay::runtime::A32GuestCallFrame frame{
        fixture.code, {0U, 0U, 0U, 0U}, {}};
    const auto result = ogplay::runtime::InvokeA32GuestCall(
        fixture.cpu, fixture.dispatcher, fixture.lifecycle, fixture.memory,
        frame, fixture.stack.Add(fixture.memory.PageSize()), return_trap, 16,
        [&calls](ogplay::cpu::Cpu& cpu,
                 const ogplay::cpu::RunResult& stopped) {
            if (stopped.immediate != 2U) {
                return ogplay::runtime::SupervisorCallProgress::not_handled;
            }
            ++calls;
            auto state = cpu.GetState();
            state.SetRegister(ogplay::cpu::CoreRegister::r0, 42U);
            cpu.SetState(state);
            return ogplay::runtime::SupervisorCallProgress::handled_idle;
        });

    CHECK(calls == 1U);
    CHECK(result.return_value == 42U);
}

TEST_CASE("renewable native call renews only at advanced boundaries") {
    RunnerFixture fixture;
    const auto return_trap = fixture.code.Add(64);
    BoundaryRefreshingCallCpu cpu(return_trap);
    fixture.Start(91);
    auto state = fixture.cpu.GetState();
    cpu.SetState(state);

    const ogplay::runtime::A32GuestCallFrame frame{
        fixture.code, {}, {}, 91U, true};
    const auto result = ogplay::runtime::InvokeA32GuestCall(
        cpu, fixture.dispatcher, fixture.lifecycle, fixture.memory, frame,
        fixture.stack.Add(fixture.memory.PageSize()), return_trap, 1U,
        [](ogplay::cpu::Cpu&, const ogplay::cpu::RunResult& stopped) {
            return stopped.immediate == 2U
                       ? ogplay::runtime::SupervisorCallProgress::handled_advanced
                       : ogplay::runtime::SupervisorCallProgress::not_handled;
        });

    CHECK(result.return_value == 91U);
    CHECK(result.ticks_consumed == 3U);
    CHECK(cpu.budgets == std::vector<std::uint64_t>{1U, 1U, 1U});
}

TEST_CASE("renewable native nanosleep residency renews its watchdog") {
    RunnerFixture fixture;
    const auto return_trap = fixture.code.Add(64);
    BoundaryRefreshingCallCpu cpu{return_trap, 0U, fixture.stack.Value()};
    fixture.Start(95U);
    auto state = fixture.cpu.GetState();
    state.SetRegister(ogplay::cpu::CoreRegister::r7, 162U);
    cpu.SetState(state);
    ogplay::hal::FixedStepClock clock{1U, 1000U};
    ogplay::runtime::BindAndroidTimeSyscalls(
        fixture.dispatcher, clock, fixture.memory);
    fixture.bus.Write32(fixture.stack, 0U, 95U);
    fixture.bus.Write32(fixture.stack.Add(4U), 1U, 95U);
    const ogplay::runtime::A32GuestCallFrame frame{
        fixture.code, {fixture.stack.Value(), 0U, 0U, 0U}, {}, 95U, true};

    const auto result = ogplay::runtime::InvokeA32GuestCall(
        cpu, fixture.dispatcher, fixture.lifecycle, fixture.memory, frame,
        fixture.stack.Add(fixture.memory.PageSize()), return_trap, 1U);
    CHECK(result.ticks_consumed == 3U);
}

TEST_CASE("renewable native syscall polling still exhausts its watchdog") {
    RunnerFixture fixture;
    SupervisorLoopCpu cpu;
    fixture.Start(92U);
    auto state = fixture.cpu.GetState();
    state.SetRegister(ogplay::cpu::CoreRegister::r7, 20U);  // getpid
    cpu.SetState(state);
    const ogplay::runtime::A32GuestCallFrame frame{
        fixture.code, {}, {}, 92U, true};

    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::runtime::InvokeA32GuestCall(
            cpu, fixture.dispatcher, fixture.lifecycle, fixture.memory, frame,
            fixture.stack.Add(fixture.memory.PageSize()), fixture.code.Add(64),
            3U)),
        "A32 guest call exhausted its tick budget: consumed=3 pc=65536 "
        "lr=65600",
        ogplay::runtime::A32GuestCallError);
}

TEST_CASE("renewable native VFS read progress ends when reads reach EOF") {
    RunnerFixture fixture;
    ogplay::runtime::VirtualFileSystem vfs;
    const std::array payload{std::byte{'a'}, std::byte{'b'}};
    vfs.PutFile("/data/progress.bin", payload, false);
    const auto descriptor =
        vfs.Open("/data/progress.bin", {.read = true});
    ogplay::runtime::BindAndroidFileSyscalls(
        fixture.dispatcher, vfs, fixture.memory);
    SupervisorLoopCpu cpu{0U, static_cast<std::uint32_t>(descriptor)};
    fixture.Start(93U);
    auto state = fixture.cpu.GetState();
    state.SetRegister(ogplay::cpu::CoreRegister::r7, 3U);  // read
    state.SetRegister(ogplay::cpu::CoreRegister::r1, fixture.stack.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r2, 1U);
    cpu.SetState(state);
    const ogplay::runtime::A32GuestCallFrame frame{
        fixture.code,
        {static_cast<std::uint32_t>(descriptor), fixture.stack.Value(), 1U, 0U},
        {}, 93U, true};

    try {
        static_cast<void>(ogplay::runtime::InvokeA32GuestCall(
            cpu, fixture.dispatcher, fixture.lifecycle, fixture.memory, frame,
            fixture.stack.Add(fixture.memory.PageSize()), fixture.code.Add(64),
            2U));
        FAIL("EOF polling unexpectedly renewed the native watchdog");
    } catch (const ogplay::runtime::A32GuestCallError& error) {
        CHECK(std::string(error.what()).find("consumed=4") !=
              std::string::npos);
    }
    CHECK(vfs.Seek(descriptor, 0, ogplay::runtime::VfsSeekWhence::current) ==
          payload.size());
}

TEST_CASE("exit request preempts a renewably progressing native call") {
    RunnerFixture fixture;
    SupervisorLoopCpu cpu{2U};
    fixture.Start(94U);
    auto state = fixture.cpu.GetState();
    state.SetRegister(ogplay::cpu::CoreRegister::r7, 20U);
    cpu.SetState(state);
    const ogplay::runtime::A32GuestCallFrame frame{
        fixture.code, {}, {}, 94U, true};
    std::uint32_t advanced_boundaries{};

    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::runtime::InvokeA32GuestCall(
            cpu, fixture.dispatcher, fixture.lifecycle, fixture.memory, frame,
            fixture.stack.Add(fixture.memory.PageSize()), fixture.code.Add(64),
            1U,
            [&fixture, &advanced_boundaries](
                ogplay::cpu::Cpu&, const ogplay::cpu::RunResult&) {
                if (++advanced_boundaries == 3U) {
                    fixture.lifecycle.RequestExit(94U, 0);
                }
                return ogplay::runtime::SupervisorCallProgress::handled_advanced;
            })),
        "A32 guest call requested thread exit before returning",
        ogplay::runtime::A32GuestCallError);
    CHECK(advanced_boundaries == 3U);
}

TEST_CASE("A32 guest call selects Thumb state from the target address") {
    RunnerFixture fixture;
    fixture.bus.Write16(fixture.code, 0x3001U);         // adds r0, #1
    fixture.bus.Write16(fixture.code.Add(2), 0x4770U);  // bx lr
    const auto return_trap = fixture.code.Add(64);
    fixture.bus.Write32(return_trap, 0xef000001U);      // svc #1
    fixture.Start(90);

    const ogplay::runtime::A32GuestCallFrame frame{
        fixture.code.Add(1), {6U, 0U, 0U, 0U}, {}};
    const auto result = ogplay::runtime::InvokeA32GuestCall(
        fixture.cpu, fixture.dispatcher, fixture.lifecycle, fixture.memory,
        frame, fixture.stack.Add(fixture.memory.PageSize()), return_trap, 16);

    CHECK(result.return_value == 7U);
    CHECK(fixture.cpu.GetState().State() ==
          ogplay::cpu::ExecutionState::a32);
}

TEST_CASE("A32 guest call rejects invalid frames and unhandled stops") {
    SUBCASE("unaligned stack shape") {
        RunnerFixture fixture;
        fixture.bus.Write32(fixture.code, 0xe12fff1eU);
        fixture.Start(87);
        const std::array<std::uint32_t, 1> stack{1U};
        const ogplay::runtime::A32GuestCallFrame frame{
            fixture.code, {}, stack};
        CHECK_THROWS_WITH_AS(
            static_cast<void>(ogplay::runtime::InvokeA32GuestCall(
                fixture.cpu, fixture.dispatcher, fixture.lifecycle,
                fixture.memory, frame,
                fixture.stack.Add(fixture.memory.PageSize()),
                fixture.code.Add(64), 8)),
            "A32 guest call stack words must preserve 8-byte alignment",
            ogplay::runtime::A32GuestCallError);
    }
    SUBCASE("unhandled trap") {
        RunnerFixture fixture;
        fixture.bus.Write32(fixture.code, 0xef000004U);
        fixture.Start(88);
        const ogplay::runtime::A32GuestCallFrame frame{
            fixture.code, {}, {}};
        try {
            static_cast<void>(ogplay::runtime::InvokeA32GuestCall(
                fixture.cpu, fixture.dispatcher, fixture.lifecycle,
                fixture.memory, frame,
                fixture.stack.Add(fixture.memory.PageSize()),
                fixture.code.Add(64), 8));
            FAIL("unhandled A32 trap unexpectedly returned");
        } catch (const ogplay::runtime::A32GuestCallError& error) {
            const std::string message = error.what();
            CHECK(message.find(
                      "\n  stop:      pc=0x00010000 state=a32 "
                      "reason=supervisor_call(1)") !=
                  std::string::npos);
            CHECK(message.find(
                      "\n  thread:    guest=88\n  registers: r0=0x00000000 "
                      "r1=0x00000000") !=
                  std::string::npos);
            CHECK(message.find(
                      "\n             r12=0x00000000 sp=0x00021000 "
                      "lr=0x00010040") !=
                  std::string::npos);
        }
    }
    SUBCASE("budget") {
        RunnerFixture fixture;
        fixture.bus.Write32(fixture.code, 0xeafffffeU);  // b .
        fixture.Start(89);
        const ogplay::runtime::A32GuestCallFrame frame{
            fixture.code, {}, {}};
        CHECK_THROWS_WITH_AS(
            static_cast<void>(ogplay::runtime::InvokeA32GuestCall(
                fixture.cpu, fixture.dispatcher, fixture.lifecycle,
                fixture.memory, frame,
                fixture.stack.Add(fixture.memory.PageSize()),
                fixture.code.Add(64), 2)),
            "A32 guest call exhausted its tick budget: consumed=2 pc=65536 "
            "lr=65600",
            ogplay::runtime::A32GuestCallError);
    }
}

TEST_CASE("A32 guest stop diagnostic names and hex-encodes a memory fault") {
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress code{0x10000U};
    memory.Map({code, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    const std::array<std::byte, 8> instruction_window{
        std::byte{0x03}, std::byte{0x68}, std::byte{0x01}, std::byte{0x33},
        std::byte{0x03}, std::byte{0x60}, std::byte{0x70}, std::byte{0x47}};
    memory.Write(code.Add(8), instruction_window, 321U);
    ogplay::cpu::A32State state;
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    state.SetThreadId(321U);
    state.SetRegister(ogplay::cpu::CoreRegister::r0, 0U);
    state.SetRegister(ogplay::cpu::CoreRegister::r1, 0x6045c4a9U);
    state.SetRegister(ogplay::cpu::CoreRegister::sp, 0x6345ff00U);
    state.SetRegister(ogplay::cpu::CoreRegister::lr, 0x6045c4adU);
    const ogplay::cpu::RunResult stopped{
        1U, ogplay::cpu::RunStopReason::memory_fault, code.Add(16), 0U, 0U,
        ogplay::cpu::CpuFault{ogplay::memory::GuestAddress{0U},
                              ogplay::memory::AccessType::read,
                              ogplay::memory::FaultReason::unmapped, 321U}};

    const auto rendered =
        ogplay::runtime::DescribeA32GuestStop(stopped, state, memory);
    CHECK(rendered.find("  stop:      pc=0x00010010 state=thumb") !=
          std::string::npos);
    CHECK(rendered.find("reason=memory_fault(4)") != std::string::npos);
    CHECK(rendered.find("  fault:     address=0x00000000 access=read(0)") !=
          std::string::npos);
    CHECK(rendered.find(
              "reason=unmapped(0)\n  thread:    guest=321\n  registers:") !=
          std::string::npos);
    CHECK(rendered.find("r1=0x6045c4a9") != std::string::npos);
    CHECK(rendered.find("  code:      pc_minus_8=0368013303607047") !=
          std::string::npos);
}
