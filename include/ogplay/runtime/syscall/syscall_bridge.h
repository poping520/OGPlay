#pragma once

#include <cstdint>
#include <optional>

#include "ogplay/cpu/cpu.h"
#include "ogplay/runtime/syscall/syscall.h"

namespace ogplay::runtime {

struct A32SyscallDispatchResult final {
    std::uint32_t number{};
    std::int32_t return_value{};
    SupervisorCallProgress progress{SupervisorCallProgress::handled_idle};
    cpu::A32State cpu_state;
};

[[nodiscard]] std::optional<A32SyscallDispatchResult>
DispatchAndroidArmSupervisorCall(cpu::Cpu& cpu,
                                 const cpu::RunResult& stop,
                                 A32SyscallDispatcher& dispatcher);

}  // namespace ogplay::runtime
