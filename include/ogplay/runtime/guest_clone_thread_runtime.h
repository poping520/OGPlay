#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>

#include "ogplay/cpu/thread_group.h"
#include "ogplay/runtime/guest_thread_runner.h"

namespace ogplay::runtime {

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
                            std::uint64_t tick_slice = 100000);

    [[nodiscard]] GuestCloneThreadJoin Join(std::uint64_t thread_id);

private:
    [[nodiscard]] std::int32_t Spawn(const GuestThreadCloneRequest& request);
    void RunChild(std::uint64_t thread_id, cpu::Cpu& cpu);

    cpu::GuestThreadGroup& threads_;
    A32SyscallDispatcher& dispatcher_;
    GuestThreadLifecycle& lifecycle_;
    memory::MemoryBus& memory_bus_;
    cpu::FutexTable& futex_table_;
    GuestThreadCloneCommitter committer_;
    std::atomic_uint64_t next_thread_id_;
    std::uint64_t tick_slice_{};
    std::mutex outcomes_mutex_;
    std::map<std::uint64_t, GuestThreadRunOutcome> outcomes_;
};

}  // namespace ogplay::runtime
