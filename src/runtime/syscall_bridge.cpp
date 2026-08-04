#include "ogplay/runtime/syscall_bridge.h"

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
    const auto value = dispatcher.Dispatch(frame);
    state.SetRegister(cpu::CoreRegister::r0,
                      std::bit_cast<std::uint32_t>(value));
    cpu.SetState(state);
    return A32SyscallDispatchResult{frame.number, value, std::move(state)};
}

}  // namespace ogplay::runtime
