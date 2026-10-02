#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include "ogplay/cpu/thread_group.h"
#include "ogplay/runtime/execution/guest_thread_runner.h"

namespace ogplay::runtime {

namespace debug { class DiagnosticState; }

struct GuestCloneThreadJoin final {
    cpu::GuestThreadExit thread;
    GuestThreadRunOutcome run;
};

class GuestCloneThreadRuntime final {
public:
    GuestCloneThreadRuntime(cpu::GuestThreadGroup& threads,
                            A32SyscallDispatcher& dispatcher,
                            GuestThreadLifecycle& lifecycle,
                            memory::AddressSpace& address_space,
                            memory::MemoryBus& memory_bus,
                            cpu::FutexTable& futex_table,
                            std::uint64_t first_child_thread_id = 2,
                            std::uint64_t tick_slice = 100000,
                            GuestSupervisorCallHandler hle_handler = {},
                            std::shared_ptr<debug::DiagnosticState> diagnostics = {},
                            std::function<void()> failure_notifier = {});

    void RethrowFailure() const;

    [[nodiscard]] GuestCloneThreadJoin Join(std::uint64_t thread_id);

private:
    [[nodiscard]] std::int32_t Spawn(const GuestThreadCloneRequest& request);
    void PublishFailure(std::uint64_t thread_id, std::exception_ptr failure);
    void RunChildBody(std::uint64_t thread_id, cpu::Cpu& cpu);

    cpu::GuestThreadGroup& threads_;
    A32SyscallDispatcher& dispatcher_;
    GuestThreadLifecycle& lifecycle_;
    memory::AddressSpace& address_space_;
    memory::MemoryBus& memory_bus_;
    cpu::FutexTable& futex_table_;
    GuestThreadCloneCommitter committer_;
    std::atomic_uint64_t next_thread_id_;
    std::uint64_t tick_slice_{};
    GuestSupervisorCallHandler hle_handler_;
    std::shared_ptr<debug::DiagnosticState> diagnostics_;
    std::function<void()> failure_notifier_;
    mutable std::mutex failure_mutex_;
    std::exception_ptr failure_;
    std::mutex outcomes_mutex_;
    std::map<std::uint64_t, GuestThreadRunOutcome> outcomes_;
};

}  // namespace ogplay::runtime
