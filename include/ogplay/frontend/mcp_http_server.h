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
}

namespace ogplay::frontend {

struct DashboardHttpConfig final {
    std::shared_ptr<agent::DashboardService> service;
    std::filesystem::path assets;
};

class McpHttpServer final {
public:
    static std::unique_ptr<McpHttpServer> Start(
        std::uint16_t port,
        agent::FrameSnapshotStore& frames,
        agent::McpInputQueue& inputs, DashboardHttpConfig dashboard = {});
    static std::unique_ptr<McpHttpServer> Start(
        std::uint16_t port,
        agent::FrameSnapshotStore& frames,
        agent::McpInputQueue& inputs,
        agent::McpSessionControl& session_control, DashboardHttpConfig dashboard = {});

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
