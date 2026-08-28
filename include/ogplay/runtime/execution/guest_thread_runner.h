#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

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
    std::function<SupervisorCallProgress(cpu::Cpu&, const cpu::RunResult&)>;
using A32GuestCallSliceObserver = std::function<void(std::uint64_t)>;

inline constexpr std::uint64_t kA32GuestCallSliceTicks = UINT64_C(20000000);

struct A32GuestCallFrame final {
    memory::GuestAddress target;
    std::array<std::uint32_t, 4> registers{};
    std::span<const std::uint32_t> stack_words;
    // Selects the prepared guest thread context. The process root is 1.
    std::uint64_t thread_id{1};
    // Long-lived JNI entry points may own a process loop. Such native frames
    // participate in watchdog renewal, but only a boundary classified as
    // handled_advanced earns renewal; idle handled work remains budgeted.
    bool renewable_native_frame{};
    // DexVM execution identity when the call originates at a Java native
    // boundary. Zero means no managed execution context is attached.
    std::uint64_t context_token{};
};

struct A32GuestCallResult final {
    std::uint64_t ticks_consumed{};
    std::uint32_t return_value{};
    std::uint32_t return_value_high{};
};

class A32GuestCallError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Renders an unhandled A32 stop for "stopped outside a handled boundary"
// reports: hex pc/registers, stop-reason and fault names alongside their
// numeric values, execution state, guest thread id and the code bytes around
// pc. Shared by the runners so every such report stays decodable without the
// enum tables or a decimal-to-hex conversion at hand.
[[nodiscard]] std::string DescribeA32GuestStop(
    const cpu::RunResult& stopped, const cpu::A32State& state,
    const memory::AddressSpace& address_space);

[[nodiscard]] SupervisorCallProgress ConsumeAndroidArmSupervisorCall(
    cpu::Cpu& cpu, const cpu::RunResult& stopped,
    A32SyscallDispatcher& dispatcher,
    const GuestSupervisorCallHandler& hle_handler = {});

[[nodiscard]] A32GuestCallResult InvokeA32GuestCall(
    cpu::Cpu& cpu, A32SyscallDispatcher& dispatcher,
    GuestThreadLifecycle& lifecycle, memory::AddressSpace& address_space,
    const A32GuestCallFrame& frame, memory::GuestAddress stack_top,
    memory::GuestAddress return_trap, std::uint64_t tick_budget,
    const GuestSupervisorCallHandler& hle_handler = {},
    const A32GuestCallSliceObserver& slice_observer = {});

[[nodiscard]] GuestThreadRunOutcome RunAndroidArmGuestThread(
    cpu::Cpu& cpu, A32SyscallDispatcher& dispatcher,
    GuestThreadLifecycle& lifecycle, memory::MemoryBus& memory_bus,
    cpu::FutexTable& futex_table, std::uint64_t tick_budget,
    const GuestSupervisorCallHandler& hle_handler = {});

}  // namespace ogplay::runtime
