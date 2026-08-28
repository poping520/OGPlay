#include "ogplay/runtime/execution/guest_thread_runner.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ogplay::runtime {
namespace {

[[nodiscard]] std::vector<std::byte> EncodeStackWords(
    const std::span<const std::uint32_t> words) {
    std::vector<std::byte> result(words.size() * sizeof(std::uint32_t));
    for (std::size_t word = 0; word < words.size(); ++word) {
        for (std::size_t byte = 0; byte < sizeof(std::uint32_t); ++byte) {
            result[word * sizeof(std::uint32_t) + byte] =
                static_cast<std::byte>(
                    (words[word] >> static_cast<unsigned>(byte * 8U)) &
                    0xffU);
        }
    }
    return result;
}

[[nodiscard]] std::string DescribeGuestCallStop(
    const cpu::RunResult& stopped, const cpu::A32State& state,
    const memory::AddressSpace& address_space) {
    return "A32 guest call stopped outside a handled boundary:\n" +
           DescribeA32GuestStop(stopped, state, address_space);
}

}  // namespace

SupervisorCallProgress ConsumeAndroidArmSupervisorCall(
    cpu::Cpu& cpu, const cpu::RunResult& stopped,
    A32SyscallDispatcher& dispatcher,
    const GuestSupervisorCallHandler& hle_handler) {
    // A fast host call deliberately exits the backend with a structured stop
    // after its noexcept callback captured the original C++ exception.  Give
    // the owning boundary first chance to restore that exception identity.
    if (stopped.reason == cpu::RunStopReason::host_call_fault) {
        return hle_handler
                   ? hle_handler(cpu, stopped)
                   : SupervisorCallProgress::not_handled;
    }
    if (stopped.reason != cpu::RunStopReason::supervisor_call) {
        return SupervisorCallProgress::not_handled;
    }
    if (const auto syscall =
            DispatchAndroidArmSupervisorCall(cpu, stopped, dispatcher)) {
        return syscall->progress;
    }
    return hle_handler
               ? hle_handler(cpu, stopped)
               : SupervisorCallProgress::not_handled;
}

A32GuestCallResult InvokeA32GuestCall(
    cpu::Cpu& cpu, A32SyscallDispatcher& dispatcher,
    GuestThreadLifecycle& lifecycle, memory::AddressSpace& address_space,
    const A32GuestCallFrame& frame, const memory::GuestAddress stack_top,
    const memory::GuestAddress return_trap,
    const std::uint64_t tick_budget,
    const GuestSupervisorCallHandler& hle_handler,
    const A32GuestCallSliceObserver& slice_observer) {
    if (frame.target.IsNull()) {
        throw A32GuestCallError("A32 guest call target is null");
    }
    if (return_trap.IsNull() || (return_trap.Value() & 3U) != 0U) {
        throw A32GuestCallError(
            "A32 guest call return trap must be word aligned");
    }
    if (stack_top.IsNull() || (stack_top.Value() & 7U) != 0U) {
        throw A32GuestCallError(
            "A32 guest call stack top must be 8-byte aligned");
    }
    if ((frame.stack_words.size() & 1U) != 0U) {
        throw A32GuestCallError(
            "A32 guest call stack words must preserve 8-byte alignment");
    }
    if (tick_budget == 0U) {
        throw A32GuestCallError(
            "A32 guest call requires a non-zero tick budget");
    }
    if (frame.stack_words.size() >
        std::numeric_limits<std::uint32_t>::max() /
            sizeof(std::uint32_t)) {
        throw A32GuestCallError("A32 guest call stack is too large");
    }
    const auto stack_bytes =
        static_cast<std::uint32_t>(frame.stack_words.size() *
                                   sizeof(std::uint32_t));
    if (stack_bytes > stack_top.Value()) {
        throw A32GuestCallError("A32 guest call stack address underflowed");
    }
    const memory::GuestAddress stack_pointer{
        stack_top.Value() - stack_bytes};

    auto state = cpu.GetState();
    if (state.ThreadId() == 0U) {
        throw A32GuestCallError(
            "A32 guest call requires a non-zero thread id");
    }
    const auto lifecycle_state = lifecycle.State(state.ThreadId());
    if (lifecycle_state.status != GuestThreadStatus::running) {
        throw A32GuestCallError(
            "A32 guest call requires a running lifecycle state");
    }

    const auto encoded_stack = EncodeStackWords(frame.stack_words);
    if (!encoded_stack.empty()) {
        address_space.Write(stack_pointer, encoded_stack, state.ThreadId());
    }
    state.SetState((frame.target.Value() & 1U) != 0U
                       ? cpu::ExecutionState::thumb
                       : cpu::ExecutionState::a32);
    state.SetRegister(cpu::CoreRegister::pc,
                      frame.target.Value() & ~1U);
    state.SetRegister(cpu::CoreRegister::lr, return_trap.Value());
    state.SetRegister(cpu::CoreRegister::sp, stack_pointer.Value());
    for (std::size_t index = 0; index < frame.registers.size(); ++index) {
        state.SetRegister(static_cast<cpu::CoreRegister>(index),
                          frame.registers[index]);
    }
    cpu.SetState(state);

    std::uint64_t consumed{};
    std::uint64_t watchdog_consumed{};
    while (watchdog_consumed < tick_budget) {
        const auto remaining = tick_budget - watchdog_consumed;
        const auto slice_ticks = std::min(remaining, kA32GuestCallSliceTicks);
        const auto stopped = cpu.Run(slice_ticks);
        if (stopped.ticks_consumed > slice_ticks) {
            throw A32GuestCallError(
                "A32 guest call CPU exceeded its tick budget");
        }
        consumed += stopped.ticks_consumed;
        watchdog_consumed += stopped.ticks_consumed;
        if (stopped.reason == cpu::RunStopReason::supervisor_call &&
            stopped.immediate == 1U && stopped.pc == return_trap) {
            const auto returned = cpu.GetState();
            return {consumed,
                    returned.Register(cpu::CoreRegister::r0),
                    returned.Register(cpu::CoreRegister::r1)};
        }
        if (stopped.reason == cpu::RunStopReason::budget_exhausted) {
            if (watchdog_consumed >= tick_budget) break;
            if (stopped.ticks_consumed == 0U) {
                throw A32GuestCallError(
                    "A32 guest call CPU made no progress");
            }
            if (slice_observer) slice_observer(consumed);
            continue;
        }
        const auto boundary_progress = ConsumeAndroidArmSupervisorCall(
            cpu, stopped, dispatcher, hle_handler);
        if (boundary_progress == SupervisorCallProgress::not_handled) {
            throw A32GuestCallError(
                DescribeGuestCallStop(stopped, cpu.GetState(), address_space));
        }
        const auto current = lifecycle.State(state.ThreadId());
        if (current.status != GuestThreadStatus::running) {
            throw A32GuestCallError(
                "A32 guest call requested thread exit before returning");
        }
        auto updated = cpu.GetState();
        updated.SetThreadPointer(current.thread_pointer);
        cpu.SetState(updated);
        if (slice_observer) slice_observer(consumed);
        if (frame.renewable_native_frame &&
            boundary_progress == SupervisorCallProgress::handled_advanced) {
            watchdog_consumed = 0U;
        }
    }
    const auto exhausted = cpu.GetState();
    throw A32GuestCallError(
        "A32 guest call exhausted its tick budget: consumed=" +
        std::to_string(consumed) + " pc=" +
        std::to_string(
            exhausted.Register(cpu::CoreRegister::pc)) +
        " lr=" +
        std::to_string(
            exhausted.Register(cpu::CoreRegister::lr)));
}

GuestThreadRunOutcome RunAndroidArmGuestThread(
    cpu::Cpu& cpu, A32SyscallDispatcher& dispatcher,
    GuestThreadLifecycle& lifecycle, memory::MemoryBus& memory_bus,
    cpu::FutexTable& futex_table, const std::uint64_t tick_budget,
    const GuestSupervisorCallHandler& hle_handler) {
    const auto initial = cpu.GetState();
    if (initial.ThreadId() == 0) {
        throw GuestThreadLifecycleError(
            "guest thread runner requires a non-zero thread id");
    }
    const auto runtime_state = lifecycle.State(initial.ThreadId());
    if (runtime_state.status != GuestThreadStatus::running) {
        throw GuestThreadLifecycleError(
            "guest thread runner requires a running lifecycle state");
    }
    cpu::RunResult last{0, cpu::RunStopReason::budget_exhausted,
                        memory::GuestAddress{
                            initial.Register(cpu::CoreRegister::pc)},
                        0, 0, std::nullopt};
    std::uint64_t consumed{};
    while (consumed < tick_budget) {
        last = cpu.Run(tick_budget - consumed);
        consumed += last.ticks_consumed;
        if (last.reason == cpu::RunStopReason::budget_exhausted) {
            return {consumed, GuestThreadRunStop::budget_exhausted, last,
                    std::nullopt};
        }
        if (last.reason != cpu::RunStopReason::supervisor_call) {
            return {consumed, GuestThreadRunStop::cpu_stop, last,
                    std::nullopt};
        }
        if (ConsumeAndroidArmSupervisorCall(
                cpu, last, dispatcher, hle_handler) ==
            SupervisorCallProgress::not_handled) {
            return {consumed, GuestThreadRunStop::unhandled_supervisor_call,
                    last, std::nullopt};
        }
        const auto state = lifecycle.State(initial.ThreadId());
        auto updated = cpu.GetState();
        updated.SetThreadPointer(state.thread_pointer);
        cpu.SetState(updated);
        if (state.status == GuestThreadStatus::exit_requested) {
            auto exit = lifecycle.CompleteExit(
                initial.ThreadId(), memory_bus, futex_table);
            return {consumed, GuestThreadRunStop::guest_exit, last,
                    std::move(exit)};
        }
    }
    return {consumed, GuestThreadRunStop::budget_exhausted, last,
            std::nullopt};
}

std::string DescribeA32GuestStop(const cpu::RunResult& stopped,
                                 const cpu::A32State& state,
                                 const memory::AddressSpace& address_space) {
    const auto hex32 = [](const std::uint32_t value) {
        std::ostringstream encoded;
        encoded << std::hex << std::nouppercase << std::setw(8)
                << std::setfill('0') << value;
        return "0x" + encoded.str();
    };
    const auto numeric =
        [](const std::string_view name, const std::uint8_t value) {
            return std::string{name} + "(" +
                   std::to_string(static_cast<unsigned>(value)) + ")";
        };
    auto result = "  stop:      pc=" + hex32(stopped.pc.Value()) + " state=" +
                  std::string{cpu::ToString(state.State())} + " reason=" +
                  numeric(cpu::ToString(stopped.reason),
                          static_cast<std::uint8_t>(stopped.reason)) +
                  " immediate=" + std::to_string(stopped.immediate);
    if (stopped.fault.has_value()) {
        result += "\n  fault:     address=" +
                  hex32(stopped.fault->address.Value()) +
                  " access=" +
                  numeric(memory::ToString(stopped.fault->access),
                          static_cast<std::uint8_t>(stopped.fault->access)) +
                  " reason=" +
                  numeric(memory::ToString(stopped.fault->reason),
                          static_cast<std::uint8_t>(stopped.fault->reason));
    }
    result += "\n  thread:    guest=" + std::to_string(state.ThreadId()) +
              "\n  registers: r0=" +
              hex32(state.Register(cpu::CoreRegister::r0)) + " r1=" +
              hex32(state.Register(cpu::CoreRegister::r1)) + " r2=" +
              hex32(state.Register(cpu::CoreRegister::r2)) + " r3=" +
              hex32(state.Register(cpu::CoreRegister::r3)) +
              "\n             r12=" +
              hex32(state.Register(cpu::CoreRegister::r12)) + " sp=" +
              hex32(state.Register(cpu::CoreRegister::sp)) + " lr=" +
              hex32(state.Register(cpu::CoreRegister::lr));
    try {
        const auto code_start = stopped.pc.Subtract(8U);
        std::array<std::byte, 24> code{};
        address_space.Read(code_start, code, state.ThreadId());
        std::ostringstream encoded;
        encoded << std::hex << std::setfill('0');
        for (const auto byte : code) {
            encoded << std::setw(2)
                    << std::to_integer<std::uint32_t>(byte);
        }
        result += "\n  code:      pc_minus_8=" + encoded.str();
    } catch (const memory::MemoryFault&) {
    }
    return result;
}

}  // namespace ogplay::runtime
