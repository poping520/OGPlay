#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>

#include "ogplay/memory/address_space.h"
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

using GuestSupervisorCallHandler =
    std::function<bool(cpu::Cpu&, const cpu::RunResult&)>;

struct A32GuestCallFrame final {
    memory::GuestAddress target;
    std::array<std::uint32_t, 4> registers{};
    std::span<const std::uint32_t> stack_words;
};

struct A32GuestCallResult final {
    std::uint64_t ticks_consumed{};
    std::uint32_t return_value{};
};

class A32GuestCallError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] bool ConsumeAndroidArmSupervisorCall(
    cpu::Cpu& cpu, const cpu::RunResult& stopped,
    A32SyscallDispatcher& dispatcher,
    const GuestSupervisorCallHandler& hle_handler = {});

[[nodiscard]] A32GuestCallResult InvokeA32GuestCall(
    cpu::Cpu& cpu, A32SyscallDispatcher& dispatcher,
    GuestThreadLifecycle& lifecycle, memory::AddressSpace& address_space,
    const A32GuestCallFrame& frame, memory::GuestAddress stack_top,
    memory::GuestAddress return_trap, std::uint64_t tick_budget,
    const GuestSupervisorCallHandler& hle_handler = {});

[[nodiscard]] GuestThreadRunOutcome RunAndroidArmGuestThread(
    cpu::Cpu& cpu, A32SyscallDispatcher& dispatcher,
    GuestThreadLifecycle& lifecycle, memory::MemoryBus& memory_bus,
    cpu::FutexTable& futex_table, std::uint64_t tick_budget,
    const GuestSupervisorCallHandler& hle_handler = {});

}  // namespace ogplay::runtime
