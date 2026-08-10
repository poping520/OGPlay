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
    std::uint64_t request_sequence{};
    std::uint64_t frame_sequence{};
    std::uint32_t x{};
    std::uint32_t y{};
    bool pressed{};
};

class McpInputQueue final {
public:
    static constexpr std::size_t kMaximumPendingClicks = 64U;

    [[nodiscard]] std::optional<std::uint64_t> TryEnqueueClick(
        std::uint64_t frame_sequence, std::uint32_t x, std::uint32_t y);
    [[nodiscard]] std::optional<McpPointerEvent> TakeNextPointerEvent();
    [[nodiscard]] std::size_t PendingClicks() const;

private:
    struct ClickState final {
        std::uint64_t request_sequence{};
        std::uint64_t frame_sequence{};
        std::uint32_t x{};
        std::uint32_t y{};
        bool down_dispatched{};
    };

    mutable std::mutex mutex_;
    std::deque<ClickState> clicks_;
    std::uint64_t next_request_sequence_{1U};
};

class McpProtocolAdapter final {
public:
    explicit McpProtocolAdapter(FrameSnapshotStore& frames,
                                std::string server_version = "0.1.0");
    McpProtocolAdapter(FrameSnapshotStore& frames, McpInputQueue& inputs,
                       std::string server_version = "0.1.0");

    // Notifications return nullopt. Requests always return one JSON-RPC response.
    [[nodiscard]] std::optional<std::string> Handle(std::string_view request) const;

private:
    FrameSnapshotStore& frames_;
    McpInputQueue* inputs_{};
    std::string server_version_;
};

}  // namespace ogplay::agent
