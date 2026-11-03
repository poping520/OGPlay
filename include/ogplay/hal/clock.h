#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace ogplay::hal {

struct ClockRate {
    std::uint32_t numerator{1};
    std::uint32_t denominator{1};

    bool operator==(const ClockRate&) const = default;
};

class Clock {
public:
    virtual ~Clock() = default;
    // Host metadata only; guest time remains instance ticks.
    [[nodiscard]] static std::string UtcTimestamp();
    [[nodiscard]] static std::uint64_t SteadyTimestampNs();
    [[nodiscard]] virtual std::uint64_t Ticks() const = 0;
    [[nodiscard]] virtual std::uint64_t TicksPerSecond() const = 0;
    [[nodiscard]] virtual bool IsPaused() const = 0;
    [[nodiscard]] virtual ClockRate Rate() const = 0;
    virtual void Reset() = 0;
    virtual void AdvanceFrames(std::uint64_t frames) = 0;
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual void SetRate(ClockRate rate) = 0;
};

class FixedStepClock final : public Clock {
public:
    FixedStepClock(std::uint64_t ticks_per_frame, std::uint64_t ticks_per_second);

    [[nodiscard]] std::uint64_t Ticks() const override;
    [[nodiscard]] std::uint64_t TicksPerSecond() const override;
    [[nodiscard]] bool IsPaused() const override;
    [[nodiscard]] ClockRate Rate() const override;
    void Reset() override;
    void AdvanceFrames(std::uint64_t frames) override;
    void Pause() override;
    void Resume() override;
    void SetRate(ClockRate rate) override;

private:
    std::uint64_t ticks_per_frame_;
    std::uint64_t ticks_per_second_;
    mutable std::mutex mutex_;
    std::uint64_t ticks_{};
    std::uint64_t rate_remainder_{};
    ClockRate rate_{};
    bool paused_{};
};

class RealtimeClock final : public Clock {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using NowFunction = std::function<TimePoint()>;

    explicit RealtimeClock(std::uint64_t ticks_per_second = 1'000'000'000, NowFunction now = {});

    [[nodiscard]] std::uint64_t Ticks() const override;
    [[nodiscard]] std::uint64_t TicksPerSecond() const override;
    [[nodiscard]] bool IsPaused() const override;
    [[nodiscard]] ClockRate Rate() const override;
    void Reset() override;
    void AdvanceFrames(std::uint64_t frames) override;
    void Pause() override;
    void Resume() override;
    void SetRate(ClockRate rate) override;

private:
    [[nodiscard]] std::uint64_t TicksAt(TimePoint now) const;
    void CommitAt(TimePoint now);

    std::uint64_t ticks_per_second_;
    NowFunction now_;
    mutable std::mutex mutex_;
    TimePoint anchor_{};
    std::uint64_t base_ticks_{};
    ClockRate rate_{};
    bool paused_{};
};

// Frame period of a host frame-rate cap; 0 frames per second means no cap and yields 0.
[[nodiscard]] std::uint64_t FramePeriodNs(std::uint32_t frames_per_second) noexcept;

// Deadline pacing for a host frame-rate cap, one instance per frame producer. A producer that is
// already slower than the cap never waits, and lateness beyond one period is dropped rather than
// caught up, so the cap only lowers the rate the producer would reach on its own.
class FramePacer final {
public:
    explicit FramePacer(std::uint64_t period_ns = 0) noexcept;

    [[nodiscard]] std::uint64_t PeriodNs() const noexcept;
    // Claims the next frame slot for a frame ready at now_ns (a SteadyTimestampNs value) and
    // returns how long to wait before releasing it; always 0 without a cap.
    [[nodiscard]] std::uint64_t Reserve(std::uint64_t now_ns) noexcept;
    // Non-blocking form for a producer polled in a loop: claims the slot and returns true once it
    // is due at now_ns, otherwise leaves the schedule unchanged and returns false.
    [[nodiscard]] bool TryClaim(std::uint64_t now_ns) noexcept;
    void Reset() noexcept;

private:
    std::uint64_t period_ns_;
    std::uint64_t next_ns_{};
    bool started_{};
};

// Blocks the calling host thread for duration_ns of steady time, with sub-millisecond precision
// where the host timer allows it.
void SleepPreciseNs(std::uint64_t duration_ns);

}  // namespace ogplay::hal
