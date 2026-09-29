#pragma once

#include <cstdint>

namespace ogplay::runtime {

struct GuestCpuExecutionConfig final {
    bool limit_parallelism{false};
    std::uint32_t max_mticks_per_second{}; // 0 = no rate ceiling
};

// Query facts and opt-in execution policy are separate. Nominal MHz never
// becomes a cycle estimate or changes Clock/tick watchdog budgets.
struct GuestCpuConfig final {
    std::uint32_t cores{1};
    std::uint32_t frequency_mhz{1000};
    GuestCpuExecutionConfig execution;
};

}  // namespace ogplay::runtime
