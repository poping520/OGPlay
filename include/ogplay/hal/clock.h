#pragma once

#include <cstdint>
#include <mutex>
#include <stdexcept>

namespace ogplay::hal {

class Clock {
public:
    virtual ~Clock() = default;
    [[nodiscard]] virtual std::uint64_t Ticks() const = 0;
    [[nodiscard]] virtual std::uint64_t TicksPerSecond() const = 0;
    virtual void Reset() = 0;
    virtual void AdvanceFrames(std::uint64_t frames) = 0;
};

class FixedStepClock final : public Clock {
public:
    FixedStepClock(const std::uint64_t ticks_per_frame,
                   const std::uint64_t ticks_per_second)
        : ticks_per_frame_(ticks_per_frame), ticks_per_second_(ticks_per_second) {
        if (ticks_per_frame_ == 0 || ticks_per_second_ == 0) {
            throw std::invalid_argument("clock rates must be greater than zero");
        }
    }

    [[nodiscard]] std::uint64_t Ticks() const override {
        std::scoped_lock lock(mutex_);
        return ticks_;
    }

    [[nodiscard]] std::uint64_t TicksPerSecond() const override {
        return ticks_per_second_;
    }

    void Reset() override {
        std::scoped_lock lock(mutex_);
        ticks_ = 0;
    }

    void AdvanceFrames(const std::uint64_t frames) override {
        std::scoped_lock lock(mutex_);
        if (frames > (UINT64_MAX - ticks_) / ticks_per_frame_) {
            throw std::overflow_error("fixed clock tick overflow");
        }
        ticks_ += frames * ticks_per_frame_;
    }

private:
    std::uint64_t ticks_per_frame_;
    std::uint64_t ticks_per_second_;
    mutable std::mutex mutex_;
    std::uint64_t ticks_{};
};

}  // namespace ogplay::hal
