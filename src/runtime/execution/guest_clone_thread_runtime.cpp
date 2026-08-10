#include "ogplay/runtime/execution/guest_clone_thread_runtime.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ogplay::runtime {

GuestCloneThreadRuntime::GuestCloneThreadRuntime(
    cpu::GuestThreadGroup& threads, A32SyscallDispatcher& dispatcher,
    GuestThreadLifecycle& lifecycle, memory::AddressSpace& address_space,
    memory::MemoryBus& memory_bus, cpu::FutexTable& futex_table,
    const std::uint64_t first_child_thread_id,
    const std::uint64_t tick_slice,
    GuestSupervisorCallHandler hle_handler)
    : threads_(threads),
      dispatcher_(dispatcher),
      lifecycle_(lifecycle),
      memory_bus_(memory_bus),
      futex_table_(futex_table),
      committer_(lifecycle, address_space),
      next_thread_id_(first_child_thread_id),
      tick_slice_(tick_slice),
      hle_handler_(std::move(hle_handler)) {
    if (first_child_thread_id == 0 || tick_slice == 0) {
        throw std::invalid_argument(
            "clone runtime requires non-zero thread id and tick slice");
    }
    BindAndroidCloneSyscall(
        dispatcher_, [this](const GuestThreadCloneRequest& request) {
            return Spawn(request);
        });
}

std::int32_t GuestCloneThreadRuntime::Spawn(
    const GuestThreadCloneRequest& request) {
    constexpr std::int32_t kEagain = 11;
    for (;;) {
        const auto child_thread_id = next_thread_id_.fetch_add(1);
        if (child_thread_id == 0 ||
            child_thread_id > static_cast<std::uint64_t>(
                                  std::numeric_limits<std::int32_t>::max())) {
            return -kEagain;
        }
        const auto committed = committer_.Commit(request, child_thread_id);
        if (committed == -kEagain) continue;
        if (committed < 0) return committed;

        auto child_state = request.parent_cpu_state;
        child_state.SetThreadId(child_thread_id);
        child_state.SetThreadPointer(
            request.thread_pointer.value_or(
                request.parent_cpu_state.ThreadPointer()));
        child_state.SetRegister(cpu::CoreRegister::sp,
                                request.child_stack.Value());
        child_state.SetRegister(cpu::CoreRegister::r0, 0);
        try {
            threads_.Spawn(
                {child_thread_id, child_state.ThreadPointer(), child_state},
                [this, child_thread_id](cpu::Cpu& cpu) {
                    RunChild(child_thread_id, cpu);
                });
        } catch (const std::exception&) {
            lifecycle_.RequestExit(child_thread_id, -1);
            static_cast<void>(lifecycle_.CompleteExit(
                child_thread_id, memory_bus_, futex_table_));
            return -kEagain;
        }
        return committed;
    }
}

void GuestCloneThreadRuntime::RunChild(const std::uint64_t thread_id,
                                       cpu::Cpu& cpu) {
    GuestThreadRunOutcome outcome;
    if (lifecycle_.State(thread_id).status ==
        GuestThreadStatus::exit_requested) {
        outcome.reason = GuestThreadRunStop::guest_exit;
        outcome.exit = lifecycle_.CompleteExit(
            thread_id, memory_bus_, futex_table_);
        std::scoped_lock lock(outcomes_mutex_);
        outcomes_.emplace(thread_id, std::move(outcome));
        return;
    }
    for (;;) {
        outcome = RunAndroidArmGuestThread(
            cpu, dispatcher_, lifecycle_, memory_bus_, futex_table_,
            tick_slice_, hle_handler_);
        if (outcome.reason != GuestThreadRunStop::budget_exhausted) break;
        const auto state = lifecycle_.State(thread_id);
        if (state.status == GuestThreadStatus::exit_requested) {
            outcome.reason = GuestThreadRunStop::guest_exit;
            outcome.exit = lifecycle_.CompleteExit(
                thread_id, memory_bus_, futex_table_);
            break;
        }
    }
    if (outcome.reason != GuestThreadRunStop::guest_exit) {
        lifecycle_.RequestExit(thread_id, -1);
        outcome.exit = lifecycle_.CompleteExit(
            thread_id, memory_bus_, futex_table_);
    }
    std::scoped_lock lock(outcomes_mutex_);
    outcomes_.emplace(thread_id, std::move(outcome));
}

GuestCloneThreadJoin GuestCloneThreadRuntime::Join(
    const std::uint64_t thread_id) {
    auto thread = threads_.Join(thread_id);
    std::scoped_lock lock(outcomes_mutex_);
    const auto found = outcomes_.find(thread_id);
    if (found == outcomes_.end()) {
        throw std::logic_error("guest clone thread has no run outcome");
    }
    auto outcome = std::move(found->second);
    outcomes_.erase(found);
    return {std::move(thread), std::move(outcome)};
}

}  // namespace ogplay::runtime
