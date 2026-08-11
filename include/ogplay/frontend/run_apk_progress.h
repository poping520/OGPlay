#pragma once

#include <cstdint>

namespace ogplay::core {
class Logger;
}

namespace ogplay::frontend {

inline constexpr std::uint64_t kLongGuestCallLogTicks = UINT64_C(1000000000);

/// 帧循环只在没有成功呈现 guest 帧的迭代里休眠让出 CPU;成功呈现的迭代立即
/// 进入下一帧,不额外征收每帧 1ms 的固定延迟。
[[nodiscard]] constexpr bool ShouldIdleSleepAfterFrameStep(
    const bool frame_presented) noexcept {
    return !frame_presented;
}

/// 以宿主 Clock ticks 节流长 guest call 期间的窗口事件泵。slice observer 在
/// 每次 supervisor call 之后都会触发，无条件泵事件会让宿主事件循环吞掉大部分
/// CPU 时间；gate 首次放行，其后每个 interval 至多放行一次。
class HostEventPumpGate final {
public:
    explicit HostEventPumpGate(std::uint64_t interval_ticks) noexcept;

    [[nodiscard]] bool ShouldPump(std::uint64_t host_ticks) noexcept;

private:
    std::uint64_t interval_ticks_{};
    std::uint64_t next_pump_ticks_{};
};

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
