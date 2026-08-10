#pragma once

#include <cstdint>
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

class FrameSnapshotStore final {
public:
    [[nodiscard]] std::optional<FrameSnapshot> Publish(FrameSnapshot frame);
    [[nodiscard]] std::optional<FrameSnapshot> Latest() const;
    [[nodiscard]] std::optional<FrameSnapshot> Take();

private:
    mutable std::mutex mutex_;
    std::optional<FrameSnapshot> latest_;
};

class McpProtocolAdapter final {
public:
    explicit McpProtocolAdapter(FrameSnapshotStore& frames,
                                std::string server_version = "0.1.0");

    // Notifications return nullopt. Requests always return one JSON-RPC response.
    [[nodiscard]] std::optional<std::string> Handle(std::string_view request) const;

private:
    FrameSnapshotStore& frames_;
    std::string server_version_;
};

}  // namespace ogplay::agent
