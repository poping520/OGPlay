#include "ogplay/runtime/syscall/syscall_bridge.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ogplay::runtime {

std::optional<A32SyscallDispatchResult> DispatchAndroidArmSupervisorCall(
    cpu::Cpu& cpu, const cpu::RunResult& stop,
    A32SyscallDispatcher& dispatcher) {
    if (stop.reason != cpu::RunStopReason::supervisor_call) {
        throw SyscallError("CPU stop is not a supervisor call");
    }
    if (stop.immediate != 0) return std::nullopt;

    auto state = cpu.GetState();
    A32SyscallFrame frame;
    frame.number = state.Register(cpu::CoreRegister::r7);
    for (std::size_t index = 0; index < frame.arguments.size(); ++index) {
        frame.arguments[index] = state.Register(
            static_cast<cpu::CoreRegister>(index));
    }
    frame.program_counter = stop.pc.Value();
    frame.link_register = state.Register(cpu::CoreRegister::lr);
    frame.thread_id = state.ThreadId();
    frame.cpu_state = state;
    const auto outcome = dispatcher.DispatchOutcome(frame);
    // ARM cacheflush validation belongs to the syscall binding; publication
    // belongs to the process-wide CPU backend and must precede guest resume.
    if (frame.number == 0x0f0002U && outcome.return_value == 0 &&
        frame.arguments[1] > frame.arguments[0]) {
        cpu.InvalidateCodeRange({memory::GuestAddress{frame.arguments[0]},
            static_cast<std::uint64_t>(frame.arguments[1]) - frame.arguments[0]});
    }
    if (outcome.restored_state) {
        state = *outcome.restored_state;
    } else {
        state.SetRegister(cpu::CoreRegister::r0,
                          std::bit_cast<std::uint32_t>(outcome.return_value));
    }
    cpu.SetState(state);
    return A32SyscallDispatchResult{
        frame.number, outcome.return_value, outcome.progress, std::move(state)};
}

}  // namespace ogplay::runtime
