#include "ogplay/agent/mcp_protocol.h"

#include <limits>
#include <stdexcept>

namespace ogplay::agent {

std::optional<std::uint64_t> McpInputQueue::TryEnqueueClick(
    const std::uint64_t frame_sequence, const std::uint32_t x,
    const std::uint32_t y) {
    return TryEnqueueSwipe(frame_sequence, x, y, x, y, 0U);
}

std::optional<std::uint64_t> McpInputQueue::TryEnqueueSwipe(
    const std::uint64_t frame_sequence, const std::uint32_t start_x,
    const std::uint32_t start_y, const std::uint32_t end_x,
    const std::uint32_t end_y, const std::uint32_t steps) {
    if (steps > kMaximumSwipeSteps) {
        throw std::invalid_argument("MCP swipe steps exceed the queue limit");
    }
    std::scoped_lock lock(mutex_);
    if (gestures_.size() >= kMaximumPendingGestures) return std::nullopt;
    if (next_request_sequence_ ==
        (std::numeric_limits<std::uint64_t>::max)()) {
        throw std::overflow_error("MCP input request sequence overflow");
    }
    const auto sequence = next_request_sequence_++;
    gestures_.push_back({sequence, frame_sequence, start_x, start_y,
                         end_x, end_y, steps, 0U});
    return sequence;
}

std::optional<McpPointerEvent> McpInputQueue::TakeNextPointerEvent() {
    std::scoped_lock lock(mutex_);
    if (gestures_.empty()) return std::nullopt;
    auto& gesture = gestures_.front();
    const bool down = gesture.next_phase == 0U;
    const bool motion = gesture.next_phase > 0U &&
                        gesture.next_phase <= gesture.move_steps;
    const auto interpolation_step = motion ? gesture.next_phase : gesture.move_steps;
    const auto interpolate = [&](const std::uint32_t start,
                                 const std::uint32_t end) {
        if (!motion) return down ? start : end;
        const auto weighted = static_cast<std::uint64_t>(start) *
                                  (gesture.move_steps - interpolation_step) +
                              static_cast<std::uint64_t>(end) * interpolation_step +
                              gesture.move_steps / 2U;
        return static_cast<std::uint32_t>(weighted / gesture.move_steps);
    };
    const McpPointerEvent result{
        gesture.request_sequence, gesture.frame_sequence,
        interpolate(gesture.start_x, gesture.end_x),
        interpolate(gesture.start_y, gesture.end_y),
        motion ? McpPointerEvent::Type::motion : McpPointerEvent::Type::button,
        down || motion};
    ++gesture.next_phase;
    if (!down && !motion) gestures_.pop_front();
    return result;
}

std::size_t McpInputQueue::PendingGestures() const {
    std::scoped_lock lock(mutex_);
    return gestures_.size();
}

}  // namespace ogplay::agent
