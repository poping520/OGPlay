#pragma once

#include <cstdint>

namespace ogplay::core {
class Logger;
}

namespace ogplay::frontend {

inline constexpr std::uint64_t kLongGuestCallLogTicks = UINT64_C(1000000000);

class RunApkGuestCallProgress final {
public:
    explicit RunApkGuestCallProgress(core::Logger& logger) noexcept;

    void Begin(std::uint64_t frame, std::uint32_t target) noexcept;
    void Observe(std::uint64_t consumed_ticks);
    void Complete(std::uint64_t consumed_ticks);

private:
    core::Logger* logger_{};
    std::uint64_t frame_{};
    std::uint32_t target_{};
    std::uint64_t last_observed_ticks_{};
    std::uint64_t next_log_ticks_{kLongGuestCallLogTicks};
    bool reported_{};
};

void LogProfileFrameProgress(core::Logger& logger, std::uint64_t frame,
                             std::uint64_t clock_ticks,
                             std::uint64_t presented,
                             std::uint64_t draws,
                             std::uint64_t clears);

}  // namespace ogplay::frontend
