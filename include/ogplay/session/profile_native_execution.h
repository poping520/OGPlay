#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/runtime/execution/guest_thread_runner.h"
#include "ogplay/session/profile_native_calls.h"

namespace ogplay::session {

struct ProfileNativeExecutionResult final {
    std::size_t call_index{};
    std::string export_name;
    runtime::A32GuestCallResult guest;
};

class ProfileNativeExecutionError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

using ProfileNativeFrameExecutor =
    std::function<runtime::A32GuestCallResult(
        const runtime::A32GuestCallFrame&)>;

[[nodiscard]] std::vector<ProfileNativeExecutionResult>
ExecuteProfileNativeInvocations(
    std::span<const ProfileNativeInvocation> invocations,
    const ProfileNativeFrameExecutor& executor);

[[nodiscard]] std::vector<ProfileNativeExecutionResult>
ExecuteProfileNativeInvocations(
    std::span<const ProfileNativeInvocation> invocations,
    cpu::Cpu& cpu, runtime::A32SyscallDispatcher& dispatcher,
    runtime::GuestThreadLifecycle& lifecycle,
    memory::AddressSpace& address_space, memory::GuestAddress stack_top,
    memory::GuestAddress return_trap, std::uint64_t tick_budget_per_call,
    const runtime::GuestSupervisorCallHandler& hle_handler = {});

}  // namespace ogplay::session
