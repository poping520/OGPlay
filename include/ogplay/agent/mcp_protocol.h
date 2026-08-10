#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::agent {

class McpSessionControl;

struct FrameSnapshot final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t sequence{};
    std::vector<std::uint8_t> rgba8;
};

struct FrameSnapshotMetadata final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t sequence{};
};

class FrameSnapshotStore final {
public:
    [[nodiscard]] std::optional<FrameSnapshot> Publish(FrameSnapshot frame);
    [[nodiscard]] std::optional<FrameSnapshot> Latest() const;
    [[nodiscard]] std::optional<FrameSnapshotMetadata> LatestMetadata() const;
    [[nodiscard]] std::optional<FrameSnapshot> Take();

private:
    mutable std::mutex mutex_;
    std::optional<FrameSnapshot> latest_;
};

struct McpPointerEvent final {
    enum class Type : std::uint8_t { button, motion };

    std::uint64_t request_sequence{};
    std::uint64_t frame_sequence{};
    std::uint32_t x{};
    std::uint32_t y{};
    Type type{};
    bool pressed{};
};

class McpInputQueue final {
public:
    static constexpr std::size_t kMaximumPendingGestures = 64U;
    static constexpr std::uint32_t kMaximumSwipeSteps = 120U;

    [[nodiscard]] std::optional<std::uint64_t> TryEnqueueClick(
        std::uint64_t frame_sequence, std::uint32_t x, std::uint32_t y);
    [[nodiscard]] std::optional<std::uint64_t> TryEnqueueSwipe(
        std::uint64_t frame_sequence, std::uint32_t start_x,
        std::uint32_t start_y, std::uint32_t end_x, std::uint32_t end_y,
        std::uint32_t steps);
    [[nodiscard]] std::optional<McpPointerEvent> TakeNextPointerEvent();
    [[nodiscard]] std::size_t PendingGestures() const;

private:
    struct GestureState final {
        std::uint64_t request_sequence{};
        std::uint64_t frame_sequence{};
        std::uint32_t start_x{};
        std::uint32_t start_y{};
        std::uint32_t end_x{};
        std::uint32_t end_y{};
        std::uint32_t move_steps{};
        std::uint32_t next_phase{};
    };

    mutable std::mutex mutex_;
    std::deque<GestureState> gestures_;
    std::uint64_t next_request_sequence_{1U};
};

class McpProtocolAdapter final {
public:
    explicit McpProtocolAdapter(FrameSnapshotStore& frames,
                                std::string server_version = "0.1.0");
    McpProtocolAdapter(FrameSnapshotStore& frames, McpInputQueue& inputs,
                       std::string server_version = "0.1.0");
    McpProtocolAdapter(FrameSnapshotStore& frames, McpInputQueue& inputs,
                       McpSessionControl& session_control,
                       std::string server_version = "0.1.0");

    // Notifications return nullopt. Requests always return one JSON-RPC response.
    [[nodiscard]] std::optional<std::string> Handle(std::string_view request) const;

private:
    FrameSnapshotStore& frames_;
    McpInputQueue* inputs_{};
    McpSessionControl* session_control_{};
    std::string server_version_;
};

}  // namespace ogplay::agent
