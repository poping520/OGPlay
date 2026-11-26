#pragma once

// Private state of the guest socket stack, shared by guest_network.cpp and
// dns_proxy.cpp. Every SocketState field is guarded by Core::mutex.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/hal/host_network.h"
#include "ogplay/runtime/net/guest_network.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime::net {

// Linux errno values.
inline constexpr int kEperm = 1;
inline constexpr int kEnoent = 2;
inline constexpr int kEintr = 4;
inline constexpr int kEio = 5;
inline constexpr int kEbadf = 9;
inline constexpr int kEagain = 11;
inline constexpr int kEnomem = 12;
inline constexpr int kEfault = 14;
inline constexpr int kEinval = 22;
inline constexpr int kEmfile = 24;
inline constexpr int kEpipe = 32;
inline constexpr int kEnosys = 38;
inline constexpr int kEnotsock = 88;
inline constexpr int kEdestaddrreq = 89;
inline constexpr int kEmsgsize = 90;
inline constexpr int kEprototype = 91;
inline constexpr int kEnoprotoopt = 92;
inline constexpr int kEprotonosupport = 93;
inline constexpr int kEsocktnosupport = 94;
inline constexpr int kEopnotsupp = 95;
inline constexpr int kEafnosupport = 97;
inline constexpr int kEaddrinuse = 98;
inline constexpr int kEaddrnotavail = 99;
inline constexpr int kEnetunreach = 101;
inline constexpr int kEconnreset = 104;
inline constexpr int kEisconn = 106;
inline constexpr int kEnotconn = 107;
inline constexpr int kEtimedout = 110;
inline constexpr int kEconnrefused = 111;
inline constexpr int kEalready = 114;
inline constexpr int kEinprogress = 115;

// Linux default net.core.wmem_default; bounds every in-process queue.
inline constexpr std::size_t kLocalQueueCapacity = 212992;
inline constexpr std::size_t kMaxDatagram = 65507;

struct ByteChannel final {
    std::deque<std::byte> data;
    // No more bytes will arrive: the writer shut down or closed.
    bool write_closed{};
    // The reader is gone: writers fail with EPIPE.
    bool read_closed{};
};

struct Datagram final {
    std::vector<std::byte> bytes;
    GuestSocketAddress from;
};

class DnsProxySession;

struct SocketState final {
    int domain{};
    int type{};
    int protocol{};
    bool closed{};

    bool bound{};
    GuestSocketAddress local;
    std::string registry_key;

    bool listening{};
    int backlog{};
    std::deque<std::shared_ptr<SocketState>> pending;

    bool connecting{};
    bool connected{};
    int pending_error{};
    bool has_peer{};
    GuestSocketAddress peer;

    // In-process stream: rx is read here, tx is the peer's rx.
    std::shared_ptr<ByteChannel> rx;
    std::shared_ptr<ByteChannel> tx;
    bool shut_read{};
    bool shut_write{};

    // In-process datagrams queued for this socket, and a socketpair peer.
    std::deque<Datagram> datagrams;
    std::size_t datagram_bytes{};
    std::weak_ptr<SocketState> pair_peer;

    // Host egress; shared so a waiter can poll it without the core lock.
    std::shared_ptr<hal::HostSocket> host;

    // Client end of a /dev/socket/dnsproxyd connection.
    std::shared_ptr<DnsProxySession> proxy;

    // SOL_SOCKET and protocol options.
    std::int64_t receive_timeout_us{};
    std::int64_t send_timeout_us{};
    bool reuse_address{};
    bool keep_alive{};
    bool broadcast{};
    bool no_delay{};
    bool v6_only{};
    std::int32_t linger_on{};
    std::int32_t linger_seconds{};
    std::int32_t receive_buffer{163840};
    std::int32_t send_buffer{163840};
    std::map<std::pair<int, int>, std::int32_t> int_options;
};

struct GuestNetworkStack::Core final {
    Core(VirtualFileSystem& filesystem, GuestNetworkConfig network) : vfs(filesystem), config(network) {}

    VirtualFileSystem& vfs;
    GuestNetworkConfig config;
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic<bool> stopped{};
    // Bound AF_UNIX names ("p" + path or "a" + abstract name).
    std::map<std::string, std::weak_ptr<SocketState>> unix_names;
    // Bound inet ports by (socket type, port); IPv4 and IPv6 share them.
    std::map<std::pair<int, std::uint16_t>, std::weak_ptr<SocketState>> inet_ports;
    std::uint16_t next_ephemeral{32768};

    // After Stop the VFS may already be retiring the descriptors; only
    // stack waiters are woken then.
    void NotifyLocked() {
        changed.notify_all();
        if (!stopped.load()) vfs.NotifyReadiness();
    }
};

[[nodiscard]] GuestResolveResult ResolveName(GuestNetworkStack::Core& core, std::string_view name);

// Server side of the netd DnsProxyListener protocol (API19 bionic clients:
// getaddrinfo, gethostbyname, gethostbyaddr). One request per connection.
class DnsProxySession final {
public:
    // Appends request bytes. Returns true once the NUL-terminated command
    // is complete; bytes after it are ignored, as netd closes the socket.
    bool Append(std::span<const std::byte> bytes);

    [[nodiscard]] bool Complete() const { return complete_; }

    // Runs the request without core locks held; may block on the resolver.
    [[nodiscard]] std::vector<std::byte> Respond(GuestNetworkStack::Core& core) const;

private:
    std::string request_;
    bool complete_{};
};

inline constexpr std::string_view kDnsProxyPath = "/dev/socket/dnsproxyd";

}  // namespace ogplay::runtime::net
