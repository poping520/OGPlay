#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>

namespace ogplay::hal {

struct ClockRate {
    std::uint32_t numerator{1};
    std::uint32_t denominator{1};

    bool operator==(const ClockRate&) const = default;
};

class Clock {
public:
    virtual ~Clock() = default;
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

    explicit RealtimeClock(std::uint64_t ticks_per_second = 1'000'000'000,
                           NowFunction now = {});

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

}  // namespace ogplay::hal
