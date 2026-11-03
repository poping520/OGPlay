#include "ogplay/hal/clock.h"

#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

#if OGPLAY_HAS_SDL3
#include <SDL3/SDL_timer.h>
#endif

namespace ogplay::hal {
std::uint64_t Clock::SteadyTimestampNs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::string Clock::UtcTimestamp() {
    return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}

namespace {

void ValidateRate(const ClockRate rate) {
    if (rate.numerator == 0 || rate.denominator == 0) {
        throw std::invalid_argument("clock rate terms must be greater than zero");
    }
}

std::uint64_t CheckedAdd(const std::uint64_t left, const std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error("clock tick overflow");
    }
    return left + right;
}

}  // namespace

FixedStepClock::FixedStepClock(const std::uint64_t ticks_per_frame, const std::uint64_t ticks_per_second)
    : ticks_per_frame_(ticks_per_frame), ticks_per_second_(ticks_per_second) {
    if (ticks_per_frame_ == 0 || ticks_per_second_ == 0) {
        throw std::invalid_argument("clock rates must be greater than zero");
    }
}

std::uint64_t FixedStepClock::Ticks() const {
    std::scoped_lock lock(mutex_);
    return ticks_;
}

std::uint64_t FixedStepClock::TicksPerSecond() const {
    return ticks_per_second_;
}

bool FixedStepClock::IsPaused() const {
    std::scoped_lock lock(mutex_);
    return paused_;
}

ClockRate FixedStepClock::Rate() const {
    std::scoped_lock lock(mutex_);
    return rate_;
}

void FixedStepClock::Reset() {
    std::scoped_lock lock(mutex_);
    ticks_ = 0;
    rate_remainder_ = 0;
    rate_ = {};
    paused_ = false;
}

void FixedStepClock::AdvanceFrames(const std::uint64_t frames) {
    std::scoped_lock lock(mutex_);
    if (paused_) throw std::logic_error("cannot advance a paused clock");
    if (frames > std::numeric_limits<std::uint64_t>::max() / ticks_per_frame_) {
        throw std::overflow_error("fixed clock frame delta overflow");
    }

    const auto unscaled = frames * ticks_per_frame_;
    const auto quotient = unscaled / rate_.denominator;
    const auto remainder = unscaled % rate_.denominator;
    if (quotient > std::numeric_limits<std::uint64_t>::max() / rate_.numerator) {
        throw std::overflow_error("fixed clock scaled delta overflow");
    }

    const auto whole = quotient * rate_.numerator;
    const auto fractional = remainder * rate_.numerator + rate_remainder_;
    const auto delta = CheckedAdd(whole, fractional / rate_.denominator);
    ticks_ = CheckedAdd(ticks_, delta);
    rate_remainder_ = fractional % rate_.denominator;
}

void FixedStepClock::Pause() {
    std::scoped_lock lock(mutex_);
    paused_ = true;
}

void FixedStepClock::Resume() {
    std::scoped_lock lock(mutex_);
    paused_ = false;
}

void FixedStepClock::SetRate(const ClockRate rate) {
    ValidateRate(rate);
    std::scoped_lock lock(mutex_);
    rate_ = rate;
    rate_remainder_ = 0;
}

RealtimeClock::RealtimeClock(const std::uint64_t ticks_per_second, NowFunction now)
    : ticks_per_second_(ticks_per_second),
      now_(now ? std::move(now) : [] { return std::chrono::steady_clock::now(); }) {
    if (ticks_per_second_ == 0) {
        throw std::invalid_argument("clock rate must be greater than zero");
    }
    anchor_ = now_();
}

std::uint64_t RealtimeClock::Ticks() const {
    std::scoped_lock lock(mutex_);
    return TicksAt(now_());
}

std::uint64_t RealtimeClock::TicksPerSecond() const {
    return ticks_per_second_;
}

bool RealtimeClock::IsPaused() const {
    std::scoped_lock lock(mutex_);
    return paused_;
}

ClockRate RealtimeClock::Rate() const {
    std::scoped_lock lock(mutex_);
    return rate_;
}

void RealtimeClock::Reset() {
    std::scoped_lock lock(mutex_);
    anchor_ = now_();
    base_ticks_ = 0;
    rate_ = {};
    paused_ = false;
}

void RealtimeClock::AdvanceFrames(const std::uint64_t) {
    throw std::logic_error("realtime clock cannot be advanced by frames");
}

void RealtimeClock::Pause() {
    std::scoped_lock lock(mutex_);
    if (paused_) return;
    CommitAt(now_());
    paused_ = true;
}

void RealtimeClock::Resume() {
    std::scoped_lock lock(mutex_);
    if (!paused_) return;
    anchor_ = now_();
    paused_ = false;
}

void RealtimeClock::SetRate(const ClockRate rate) {
    ValidateRate(rate);
    std::scoped_lock lock(mutex_);
    CommitAt(now_());
    rate_ = rate;
}

std::uint64_t RealtimeClock::TicksAt(const TimePoint now) const {
    if (paused_ || now <= anchor_) return base_ticks_;
    const auto seconds = std::chrono::duration<long double>(now - anchor_).count();
    const auto scaled = seconds * static_cast<long double>(ticks_per_second_) *
                        static_cast<long double>(rate_.numerator) / static_cast<long double>(rate_.denominator);
    if (!std::isfinite(scaled) ||
        scaled > static_cast<long double>(std::numeric_limits<std::uint64_t>::max() - base_ticks_)) {
        throw std::overflow_error("realtime clock tick overflow");
    }
    return base_ticks_ + static_cast<std::uint64_t>(scaled);
}

void RealtimeClock::CommitAt(const TimePoint now) {
    base_ticks_ = TicksAt(now);
    anchor_ = now;
}

std::uint64_t FramePeriodNs(const std::uint32_t frames_per_second) noexcept {
    return frames_per_second == 0 ? 0 : 1'000'000'000ULL / frames_per_second;
}

FramePacer::FramePacer(const std::uint64_t period_ns) noexcept : period_ns_(period_ns) {}

std::uint64_t FramePacer::PeriodNs() const noexcept {
    return period_ns_;
}

std::uint64_t FramePacer::Reserve(const std::uint64_t now_ns) noexcept {
    if (period_ns_ == 0) return 0;
    if (!started_ || now_ns >= next_ns_) {
        // On time or late: release now. Within one period the schedule is kept so jitter does not
        // cost frames; further behind, it restarts from now instead of bursting to catch up.
        next_ns_ = started_ && now_ns - next_ns_ < period_ns_ ? next_ns_ + period_ns_ : now_ns + period_ns_;
        started_ = true;
        return 0;
    }
    const auto wait = next_ns_ - now_ns;
    next_ns_ += period_ns_;
    return wait;
}

bool FramePacer::TryClaim(const std::uint64_t now_ns) noexcept {
    if (period_ns_ != 0 && started_ && now_ns < next_ns_) return false;
    static_cast<void>(Reserve(now_ns));
    return true;
}

void FramePacer::Reset() noexcept {
    started_ = false;
}

void SleepPreciseNs(const std::uint64_t duration_ns) {
    if (duration_ns == 0) return;
#if OGPLAY_HAS_SDL3
    // Uses the high-resolution waitable timer on Windows, where a plain sleep rounds to the
    // system timer tick, and finishes the last stretch by yielding.
    SDL_DelayPrecise(duration_ns);
#else
    std::this_thread::sleep_for(std::chrono::nanoseconds(duration_ns));
#endif
}

}  // namespace ogplay::hal
