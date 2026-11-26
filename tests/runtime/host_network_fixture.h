#pragma once

// Host-side fixtures for guest network tests: a loopback TCP server on the
// host and a host egress that sends test addresses to it. Guest loopback
// stays in process, so tests reach host servers through TEST-NET-3 names.

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "ogplay/hal/host_network.h"

namespace ogplay::test::host_network {

// 203.0.113.10: the guest-visible address of every redirected name.
inline constexpr std::array<std::uint8_t, 4> kRedirectAddress{203, 0, 113, 10};

// Accepts host loopback connections and serves each with `handler` on its own thread.
class HostTcpServer final {
public:
    using Handler = std::function<void(std::intptr_t socket)>;
    explicit HostTcpServer(Handler handler);
    ~HostTcpServer();
    HostTcpServer(const HostTcpServer&) = delete;
    HostTcpServer& operator=(const HostTcpServer&) = delete;

    [[nodiscard]] std::uint16_t Port() const { return port_; }

    [[nodiscard]] std::uint32_t Accepted() const { return accepted_.load(); }

    // Blocking helpers for handlers.
    static std::string ReceiveSome(std::intptr_t socket);
    static void SendAll(std::intptr_t socket, std::string_view bytes);

private:
    std::intptr_t listener_{-1};
    std::uint16_t port_{};
    std::atomic<bool> stopping_{};
    std::atomic<std::uint32_t> accepted_{};
    std::thread thread_;
    std::vector<std::thread> workers_;
    Handler handler_;
};

// The operating system egress, except that `names` resolve to kRedirectAddress
// and connections to it go to 127.0.0.1 on the same port.
class RedirectHostNetwork final : public hal::HostNetwork {
public:
    explicit RedirectHostNetwork(std::vector<std::string> names);
    std::unique_ptr<hal::HostSocket> Open(hal::HostAddressFamily family, hal::HostSocketType type, int* error) override;
    hal::HostResolveResult Resolve(std::string_view name) override;
    std::optional<std::string> ReverseLookup(const hal::HostSocketAddress& address) override;

    [[nodiscard]] std::uint32_t Resolutions() const { return resolutions_.load(); }

private:
    std::unique_ptr<hal::HostNetwork> system_;
    std::vector<std::string> names_;
    std::atomic<std::uint32_t> resolutions_{};
};

}  // namespace ogplay::test::host_network
