#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace ogplay::agent {
class FrameSnapshotStore;
class McpInputQueue;
class McpSessionControl;
}

namespace ogplay::frontend {

class McpHttpServer final {
public:
    static std::unique_ptr<McpHttpServer> Start(
        std::uint16_t port,
        agent::FrameSnapshotStore& frames,
        agent::McpInputQueue& inputs);
    static std::unique_ptr<McpHttpServer> Start(
        std::uint16_t port,
        agent::FrameSnapshotStore& frames,
        agent::McpInputQueue& inputs,
        agent::McpSessionControl& session_control);

    ~McpHttpServer();

    McpHttpServer(const McpHttpServer&) = delete;
    McpHttpServer& operator=(const McpHttpServer&) = delete;

    [[nodiscard]] std::uint16_t Port() const noexcept;
    [[nodiscard]] std::string Endpoint() const;

private:
    struct Impl;

    explicit McpHttpServer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::frontend
