#include "ogplay/runtime/guest_thread_runner.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace ogplay::runtime {

GuestThreadRunOutcome RunAndroidArmGuestThread(
    cpu::Cpu& cpu, A32SyscallDispatcher& dispatcher,
    GuestThreadLifecycle& lifecycle, memory::MemoryBus& memory_bus,
    cpu::FutexTable& futex_table, const std::uint64_t tick_budget) {
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
        const auto dispatched = DispatchAndroidArmSupervisorCall(
            cpu, last, dispatcher);
        if (!dispatched.has_value()) {
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

}  // namespace ogplay::runtime
