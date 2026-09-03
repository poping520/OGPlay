#include "ogplay/frontend/run_apk_progress.h"

#include "ogplay/core/logger.h"

namespace ogplay::frontend {
namespace {

constexpr core::RateLimitPolicy kUnrestrictedLog{
    .mode = core::RateLimitMode::none};

}  // namespace

HostEventThreadGate::HostEventThreadGate() noexcept
    : owner_(std::this_thread::get_id()) {}

bool HostEventThreadGate::IsOwnerThread() const noexcept {
    return std::this_thread::get_id() == owner_;
}

HostEventPumpGate::HostEventPumpGate(
    const std::uint64_t interval_ticks) noexcept
    : interval_ticks_(interval_ticks) {}

bool HostEventPumpGate::ShouldPump(const std::uint64_t host_ticks) noexcept {
    if (host_ticks < next_pump_ticks_) return false;
    next_pump_ticks_ = host_ticks + interval_ticks_;
    return true;
}

RunApkGuestCallProgress::RunApkGuestCallProgress(
    core::Logger& logger) noexcept
    : logger_(&logger) {}

void RunApkGuestCallProgress::Begin(const std::uint64_t frame,
                                    const std::uint32_t target) noexcept {
    frame_ = frame;
    target_ = target;
    last_observed_ticks_ = 0U;
    next_log_ticks_ = kLongGuestCallLogTicks;
    reported_ = false;
}

void RunApkGuestCallProgress::Observe(const std::uint64_t consumed_ticks) {
    if (consumed_ticks < last_observed_ticks_) {
        Begin(frame_, target_);
    }
    last_observed_ticks_ = consumed_ticks;
    if (consumed_ticks < next_log_ticks_) return;
    logger_->Write(
        core::LogLevel::info, "runtime.guest_call",
        "long guest call is still running",
        {.frame = frame_, .guest_ticks = consumed_ticks},
        {{"target", core::GuestAddress{target_}},
         {"ticks_consumed", consumed_ticks}},
        kUnrestrictedLog);
    reported_ = true;
    next_log_ticks_ =
        (consumed_ticks / kLongGuestCallLogTicks + 1U) *
        kLongGuestCallLogTicks;
}

void RunApkGuestCallProgress::Complete(const std::uint64_t consumed_ticks) {
    if (!reported_) return;
    logger_->Write(
        core::LogLevel::info, "runtime.guest_call",
        "long guest call completed",
        {.frame = frame_, .guest_ticks = consumed_ticks},
        {{"target", core::GuestAddress{target_}},
         {"ticks_consumed", consumed_ticks}},
        kUnrestrictedLog);
}

void LogProfileFrameProgress(core::Logger& logger, const std::uint64_t frame,
                             const std::uint64_t clock_ticks,
                             const std::uint64_t presented,
                             const std::uint64_t draws,
                             const std::uint64_t clears) {
    if (frame != 1U && frame % 600U != 0U) return;
    logger.Write(core::LogLevel::debug, "frontend.run_apk",
                 "Profile frame completed",
                 {.frame = frame, .guest_ticks = clock_ticks},
                 {{"presented", presented},
                  {"draws", draws},
                  {"clears", clears}},
                 kUnrestrictedLog);
}

}  // namespace ogplay::frontend
