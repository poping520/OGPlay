#pragma once

// Host network egress for the guest socket stack: non-blocking host sockets
// and the host resolver. Every status is 0/positive or a negative *Linux*
// errno (the guest ABI), never a host errno.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::hal {

enum class HostAddressFamily : std::uint8_t { inet, inet6 };
enum class HostSocketType : std::uint8_t { stream, datagram };

struct HostSocketAddress final {
    HostAddressFamily family{HostAddressFamily::inet};
    // Network byte order; inet uses the first four bytes.
    std::array<std::uint8_t, 16> address{};
    std::uint16_t port{};  // Host byte order.
    std::uint32_t scope_id{};

    friend bool operator==(const HostSocketAddress&, const HostSocketAddress&) = default;
};

enum class HostSocketOption : std::uint8_t {
    no_delay,
    keep_alive,
    receive_buffer,
    send_buffer,
    broadcast,
    reuse_address,
    ttl,
};

struct HostReadiness final {
    bool readable{};
    bool writable{};
    bool error{};
    bool hangup{};
};

class HostSocket {
public:
    virtual ~HostSocket() = default;
    // All operations are non-blocking. Connect returns -EINPROGRESS (115)
    // while the handshake is pending; TakeError then reports SO_ERROR.
    [[nodiscard]] virtual int Connect(const HostSocketAddress& address) = 0;
    [[nodiscard]] virtual int TakeError() = 0;
    [[nodiscard]] virtual int Bind(const HostSocketAddress& address) = 0;
    [[nodiscard]] virtual std::int64_t Send(std::span<const std::byte> bytes) = 0;
    [[nodiscard]] virtual std::int64_t SendTo(std::span<const std::byte> bytes, const HostSocketAddress& to) = 0;
    [[nodiscard]] virtual std::int64_t Receive(std::span<std::byte> bytes, bool peek) = 0;
    [[nodiscard]] virtual std::int64_t ReceiveFrom(std::span<std::byte> bytes, bool peek, HostSocketAddress& from) = 0;
    // how: 0 read, 1 write, 2 both.
    [[nodiscard]] virtual int Shutdown(int how) = 0;
    [[nodiscard]] virtual int LocalAddress(HostSocketAddress& address) = 0;
    [[nodiscard]] virtual int PeerAddress(HostSocketAddress& address) = 0;
    [[nodiscard]] virtual int SetOption(HostSocketOption option, int value) = 0;
    [[nodiscard]] virtual std::optional<std::size_t> Available() = 0;
    // Waits at most `timeout` for any requested readiness, then reports the
    // current state. A zero timeout only samples.
    [[nodiscard]] virtual HostReadiness Wait(bool read, bool write, std::chrono::milliseconds timeout) = 0;
};

struct HostResolveResult final {
    // False when the name has no address or the resolver failed.
    bool found{};
    std::vector<HostSocketAddress> addresses;
    std::string canonical_name;
};

class HostNetwork {
public:
    virtual ~HostNetwork() = default;
    // Null with *error set to a negative Linux errno on failure.
    [[nodiscard]] virtual std::unique_ptr<HostSocket> Open(HostAddressFamily family, HostSocketType type,
                                                           int* error) = 0;
    // Blocking host resolver lookup; ports are zero.
    [[nodiscard]] virtual HostResolveResult Resolve(std::string_view name) = 0;
    [[nodiscard]] virtual std::optional<std::string> ReverseLookup(const HostSocketAddress& address) = 0;
};

// The operating system's sockets and resolver.
[[nodiscard]] std::unique_ptr<HostNetwork> CreateSystemHostNetwork();

}  // namespace ogplay::hal
