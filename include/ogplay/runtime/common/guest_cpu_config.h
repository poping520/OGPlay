#pragma once

#include <cstdint>
#include <string>

namespace ogplay::runtime {

struct GuestCpuExecutionConfig final {
    bool limit_parallelism{false};
    std::uint32_t max_mticks_per_second{}; // 0 = no rate ceiling
};

// /proc/cpuinfo identity of the modelled SoC (device preset). Empty hardware
// keeps the virtual Cortex-A9 r0p0 default; Features never changes because it
// describes what the Dynarmic backend executes.
struct GuestCpuIdentity final {
    std::string hardware;  // "Hardware" line
    std::uint32_t midr{};  // implementer/variant/part/revision, ARMv7 CPUID scheme
};

// Query facts and opt-in execution policy are separate. Nominal MHz never
// becomes a cycle estimate or changes Clock/tick watchdog budgets.
struct GuestCpuConfig final {
    std::uint32_t cores{1};
    std::uint32_t frequency_mhz{1000};
    GuestCpuExecutionConfig execution;
    GuestCpuIdentity identity;
};

}  // namespace ogplay::runtime
