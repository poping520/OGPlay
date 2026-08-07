#include "ogplay/session/profile_native_execution.h"

#include <cstddef>
#include <exception>
#include <span>
#include <string>
#include <vector>

namespace ogplay::session {
namespace {

void ValidateInvocations(
    const std::span<const ProfileNativeInvocation> invocations) {
    std::size_t previous_index{};
    bool first = true;
    for (const auto& invocation : invocations) {
        if (invocation.export_name.empty() || invocation.address.IsNull() ||
            (invocation.stack_words.size() & 1U) != 0U) {
            throw ProfileNativeExecutionError(
                "profile native execution contains an incomplete invocation");
        }
        if (!first && invocation.call_index <= previous_index) {
            throw ProfileNativeExecutionError(
                "profile native execution call indexes are not strictly ordered");
        }
        previous_index = invocation.call_index;
        first = false;
    }
}

}  // namespace

std::vector<ProfileNativeExecutionResult> ExecuteProfileNativeInvocations(
    const std::span<const ProfileNativeInvocation> invocations,
    const ProfileNativeFrameExecutor& executor) {
    ValidateInvocations(invocations);
    if (!executor) {
        throw ProfileNativeExecutionError(
            "profile native execution requires a frame executor");
    }
    std::vector<ProfileNativeExecutionResult> result;
    result.reserve(invocations.size());
    for (const auto& invocation : invocations) {
        try {
            const runtime::A32GuestCallFrame frame{
                invocation.address, invocation.registers,
                invocation.stack_words};
            result.push_back(
                {invocation.call_index, invocation.export_name,
                 executor(frame)});
        } catch (const std::exception& error) {
            throw ProfileNativeExecutionError(
                "profile native call " +
                std::to_string(invocation.call_index) + " (" +
                invocation.export_name + ") failed: " + error.what());
        }
    }
    return result;
}

std::vector<ProfileNativeExecutionResult> ExecuteProfileNativeInvocations(
    const std::span<const ProfileNativeInvocation> invocations,
    cpu::Cpu& cpu, runtime::A32SyscallDispatcher& dispatcher,
    runtime::GuestThreadLifecycle& lifecycle,
    memory::AddressSpace& address_space,
    const memory::GuestAddress stack_top,
    const memory::GuestAddress return_trap,
    const std::uint64_t tick_budget_per_call,
    const runtime::GuestSupervisorCallHandler& hle_handler) {
    if (stack_top.IsNull() || !stack_top.IsAligned(8) ||
        return_trap.IsNull() || tick_budget_per_call == 0) {
        throw ProfileNativeExecutionError(
            "profile native execution requires aligned process memory and a tick budget");
    }
    return ExecuteProfileNativeInvocations(
        invocations,
        [&cpu, &dispatcher, &lifecycle, &address_space, stack_top,
         return_trap, tick_budget_per_call,
         &hle_handler](const runtime::A32GuestCallFrame& frame) {
            return runtime::InvokeA32GuestCall(
                cpu, dispatcher, lifecycle, address_space, frame,
                stack_top, return_trap, tick_budget_per_call,
                hle_handler);
        });
}

}  // namespace ogplay::session
