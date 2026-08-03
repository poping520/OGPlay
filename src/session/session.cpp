#include "ogplay/session/session.h"

#include <algorithm>
#include <stdexcept>

namespace ogplay::session {

Session::Session(hal::Clock& clock) : clock_(clock) {}

SessionState Session::OpenEmpty() {
    std::scoped_lock lock(mutex_);
    Require(Phase::idle, "session.open");
    clock_.Reset();
    frame_ = 0;
    ++generation_;
    session_id_ = "empty-" + std::to_string(generation_);
    phase_ = Phase::running;
    return StateLocked();
}

SessionState Session::Close() {
    std::scoped_lock lock(mutex_);
    if (phase_ == Phase::idle) throw std::logic_error("session.close requires an open session");
    phase_ = Phase::idle;
    frame_ = 0;
    clock_.Reset();
    session_id_.clear();
    return StateLocked();
}

SessionState Session::State() const {
    std::scoped_lock lock(mutex_);
    return StateLocked();
}

SessionState Session::Step(const std::uint64_t frames) {
    if (frames == 0) throw std::invalid_argument("run.step frames must be greater than zero");
    std::scoped_lock lock(mutex_);
    Require(Phase::running, "run.step");
    if (frames > UINT64_MAX - frame_) throw std::overflow_error("session frame overflow");
    clock_.AdvanceFrames(frames);
    frame_ += frames;
    return StateLocked();
}

UntilResult Session::UntilFrame(const std::uint64_t target_frame,
                                const std::uint64_t max_frames) {
    std::scoped_lock lock(mutex_);
    Require(Phase::running, "run.until");
    const auto required = target_frame > frame_ ? target_frame - frame_ : 0;
    const auto advance = std::min(required, max_frames);
    if (advance != 0) {
        clock_.AdvanceFrames(advance);
        frame_ += advance;
    }
    return {frame_ >= target_frame, StateLocked()};
}

SessionState Session::Pause() {
    std::scoped_lock lock(mutex_);
    Require(Phase::running, "run.pause");
    clock_.Pause();
    phase_ = Phase::paused;
    return StateLocked();
}

SessionState Session::Resume() {
    std::scoped_lock lock(mutex_);
    Require(Phase::paused, "run.resume");
    clock_.Resume();
    phase_ = Phase::running;
    return StateLocked();
}

SessionState Session::StateLocked() const {
    const auto ticks = clock_.Ticks();
    return {
        .phase = phase_, .session_id = session_id_, .frame = frame_,
        .guest_ticks = ticks,
        .wall_ms = ticks * 1000U / clock_.TicksPerSecond(),
        .guest_threads = 0,
    };
}

void Session::Require(const Phase phase, const std::string_view operation) const {
    if (phase_ != phase) {
        throw std::logic_error(std::string(operation) + " is invalid while session is " +
                               std::string(ToString(phase_)));
    }
}

std::string_view ToString(const Phase phase) noexcept {
    switch (phase) {
    case Phase::idle: return "idle";
    case Phase::running: return "running";
    case Phase::paused: return "paused";
    }
    return "unknown";
}

}  // namespace ogplay::session
