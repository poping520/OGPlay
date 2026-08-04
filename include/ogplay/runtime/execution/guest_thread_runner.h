#pragma once

#include <cstdint>
#include <optional>

#include "ogplay/runtime/syscall/guest_thread_lifecycle.h"
#include "ogplay/runtime/syscall/syscall_bridge.h"

namespace ogplay::runtime {

enum class GuestThreadRunStop : std::uint8_t {
    budget_exhausted,
    guest_exit,
    cpu_stop,
    unhandled_supervisor_call,
};

struct GuestThreadRunOutcome final {
    std::uint64_t ticks_consumed{};
    GuestThreadRunStop reason{GuestThreadRunStop::budget_exhausted};
    cpu::RunResult cpu_stop;
    std::optional<GuestThreadExitCompletion> exit;
};

[[nodiscard]] GuestThreadRunOutcome RunAndroidArmGuestThread(
    cpu::Cpu& cpu, A32SyscallDispatcher& dispatcher,
    GuestThreadLifecycle& lifecycle, memory::MemoryBus& memory_bus,
    cpu::FutexTable& futex_table, std::uint64_t tick_budget);

}  // namespace ogplay::runtime
