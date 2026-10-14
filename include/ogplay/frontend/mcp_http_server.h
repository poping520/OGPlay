#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace ogplay::agent {
class FrameSnapshotStore;
class McpInputQueue;
class McpSessionControl;
class DashboardService;
class ControlEditorChannel;
}

namespace ogplay::frontend {

struct DashboardHttpConfig final {
    std::shared_ptr<agent::DashboardService> service;
    std::filesystem::path assets;
};

// Key editor (ADR-0134): static page under /controls/ from `assets` (empty: no page, 404) and
// /controls/rpc, where every request needs "Authorization: Bearer <token>".
struct ControlHttpConfig final {
    std::shared_ptr<agent::ControlEditorChannel> channel;
    std::string token;
    std::filesystem::path assets;
};

class McpHttpServer final {
public:
    static std::unique_ptr<McpHttpServer> Start(
        std::uint16_t port,
        agent::FrameSnapshotStore& frames,
        agent::McpInputQueue& inputs, DashboardHttpConfig dashboard = {},
        ControlHttpConfig controls = {});
    static std::unique_ptr<McpHttpServer> Start(
        std::uint16_t port,
        agent::FrameSnapshotStore& frames,
        agent::McpInputQueue& inputs,
        agent::McpSessionControl& session_control, DashboardHttpConfig dashboard = {},
        ControlHttpConfig controls = {});

    // Editor-only service (ADR-0134) for sessions started without MCP: /controls/ and
    // /controls/rpc; /mcp and /dash/ are absent. Port 0 picks a free port.
    static std::unique_ptr<McpHttpServer> StartKeyEditor(
        std::uint16_t port, agent::FrameSnapshotStore& frames, agent::McpInputQueue& inputs,
        ControlHttpConfig controls);

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
