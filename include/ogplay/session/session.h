#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include "ogplay/hal/clock.h"

namespace ogplay::session {

enum class Phase : std::uint8_t { idle, running, paused };

struct SessionState {
    Phase phase{Phase::idle};
    std::string session_id;
    std::uint64_t frame{};
    std::uint64_t guest_ticks{};
    std::uint64_t wall_ms{};
    std::uint32_t guest_threads{};
};

struct UntilResult {
    bool reached{};
    SessionState state;
};

class Session final {
public:
    explicit Session(hal::Clock& clock);

    [[nodiscard]] SessionState OpenEmpty();
    [[nodiscard]] SessionState Close();
    [[nodiscard]] SessionState State() const;
    [[nodiscard]] SessionState Step(std::uint64_t frames = 1);
    [[nodiscard]] UntilResult UntilFrame(std::uint64_t target_frame,
                                         std::uint64_t max_frames);
    [[nodiscard]] SessionState Pause();
    [[nodiscard]] SessionState Resume();

private:
    [[nodiscard]] SessionState StateLocked() const;
    void Require(Phase phase, std::string_view operation) const;

    hal::Clock& clock_;
    mutable std::mutex mutex_;
    Phase phase_{Phase::idle};
    std::string session_id_;
    std::uint64_t frame_{};
    std::uint64_t generation_{};
};

[[nodiscard]] std::string_view ToString(Phase phase) noexcept;

}  // namespace ogplay::session
