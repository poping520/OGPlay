#pragma once

// Guest socket stack shared by the ARM syscall layer and the libcore.io.Posix
// natives. Sockets are VFS descriptor objects, so both callers see the same
// descriptor numbers, dup/fork/close-on-exec and poll readiness. AF_UNIX and
// loopback traffic stay in process; other destinations use the injected host
// egress only in online mode. Results are >= 0 or a negative Linux errno.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::hal {
class HostNetwork;
}

namespace ogplay::core {
class Logger;
}

namespace ogplay::runtime {
class VirtualFileSystem;
}

namespace ogplay::runtime::net {

// Linux ARM EABI socket ABI values.
inline constexpr int kAfUnspec = 0;
inline constexpr int kAfUnix = 1;
inline constexpr int kAfInet = 2;
inline constexpr int kAfInet6 = 10;
inline constexpr int kSockStream = 1;
inline constexpr int kSockDgram = 2;
inline constexpr int kSockNonBlock = 0x800;
inline constexpr int kSockCloexec = 0x80000;
inline constexpr int kSolSocket = 1;
inline constexpr int kIpprotoIp = 0;
inline constexpr int kIpprotoTcp = 6;
inline constexpr int kIpprotoUdp = 17;
inline constexpr int kIpprotoIpv6 = 41;
inline constexpr int kMsgOob = 0x1;
inline constexpr int kMsgPeek = 0x2;
inline constexpr int kMsgTrunc = 0x20;
inline constexpr int kMsgDontWait = 0x40;
inline constexpr int kMsgWaitAll = 0x100;
inline constexpr int kMsgNoSignal = 0x4000;
inline constexpr int kMsgMore = 0x8000;

// disabled: AF_UNIX and loopback only, other destinations are unreachable,
// as on a phone whose Wi-Fi and mobile data are off. loopback is the same
// stack for test runs. online: other destinations use the host egress.
enum class NetworkMode : std::uint8_t { disabled, loopback, online };

// Launcher setting values: disabled, loopback, allow.
[[nodiscard]] std::optional<NetworkMode> ParseNetworkMode(std::string_view text);
[[nodiscard]] std::string_view NetworkModeName(NetworkMode mode);

struct GuestSocketAddress final {
    int family{kAfUnspec};
    // Network byte order; AF_INET uses the first four bytes.
    std::array<std::uint8_t, 16> address{};
    std::uint16_t port{};
    std::uint32_t flow_info{};
    std::uint32_t scope_id{};
    // AF_UNIX: a path, or an abstract name without its leading NUL.
    std::string path;
    bool abstract_name{};

    [[nodiscard]] static GuestSocketAddress Inet4(std::array<std::uint8_t, 4> bytes, std::uint16_t port);
    [[nodiscard]] static GuestSocketAddress Unix(std::string path, bool abstract_name = false);
};

// Linux ARM sockaddr codec (sockaddr_un, sockaddr_in, sockaddr_in6).
[[nodiscard]] int DecodeSockaddr(std::span<const std::byte> bytes, GuestSocketAddress& address);
[[nodiscard]] std::vector<std::byte> EncodeSockaddr(const GuestSocketAddress& address);
[[nodiscard]] std::optional<GuestSocketAddress> ParseNumericAddress(std::string_view text);
[[nodiscard]] std::string FormatNumericAddress(const GuestSocketAddress& address);
// 127/8, ::1 and their IPv4-mapped forms.
[[nodiscard]] bool IsLoopback(const GuestSocketAddress& address);

struct GuestResolveResult final {
    bool found{};
    std::vector<GuestSocketAddress> addresses;
    std::string canonical_name;
};

struct GuestNetworkConfig final {
    NetworkMode mode{NetworkMode::disabled};
    // Required for online mode; owned by the session.
    hal::HostNetwork* host{};
    // Wi-Fi address of the emulated device, reported as the local address
    // of host-backed sockets instead of the host computer's address.
    std::array<std::uint8_t, 4> device_ipv4{192, 168, 1, 100};
    // SO_PEERCRED identity of the guest process.
    std::int32_t pid{};
    std::uint32_t uid{};
    std::uint32_t gid{};
    // Optional: records name lookups and egress connects (runtime.net).
    core::Logger* logger{};
};

class GuestNetworkStack final {
public:
    GuestNetworkStack(VirtualFileSystem& vfs, GuestNetworkConfig config);
    ~GuestNetworkStack();
    GuestNetworkStack(const GuestNetworkStack&) = delete;
    GuestNetworkStack& operator=(const GuestNetworkStack&) = delete;

    [[nodiscard]] NetworkMode Mode() const;
    [[nodiscard]] const GuestNetworkConfig& Config() const;
    [[nodiscard]] VirtualFileSystem& FileSystem() const;

    // type may carry SOCK_NONBLOCK and SOCK_CLOEXEC.
    [[nodiscard]] int Socket(int domain, int type, int protocol);
    [[nodiscard]] int SocketPair(int domain, int type, int protocol, std::array<int, 2>& descriptors);
    [[nodiscard]] int Bind(int descriptor, const GuestSocketAddress& address);
    [[nodiscard]] int Connect(int descriptor, const GuestSocketAddress& address);
    [[nodiscard]] int Listen(int descriptor, int backlog);
    // flags: SOCK_NONBLOCK / SOCK_CLOEXEC for the accepted descriptor.
    [[nodiscard]] int Accept(int descriptor, GuestSocketAddress* peer, int flags);
    [[nodiscard]] int LocalName(int descriptor, GuestSocketAddress& address);
    [[nodiscard]] int PeerName(int descriptor, GuestSocketAddress& address);
    [[nodiscard]] std::int64_t Send(int descriptor, std::span<const std::byte> bytes, int flags,
                                    const GuestSocketAddress* destination);
    // A datagram longer than `bytes` is truncated; MSG_TRUNC returns its full size.
    [[nodiscard]] std::int64_t Receive(int descriptor, std::span<std::byte> bytes, int flags,
                                       GuestSocketAddress* source);
    [[nodiscard]] int ShutdownSocket(int descriptor, int how);
    [[nodiscard]] int SetOption(int descriptor, int level, int name, std::span<const std::byte> value);
    // Writes the Linux ARM value layout into `value`.
    [[nodiscard]] int GetOption(int descriptor, int level, int name, std::vector<std::byte>& value);
    [[nodiscard]] bool IsSocket(int descriptor) const;

    // Numeric names, the API19 hosts file entries, then (online only) the host resolver.
    [[nodiscard]] GuestResolveResult Resolve(std::string_view name);
    [[nodiscard]] std::optional<std::string> ReverseLookup(const GuestSocketAddress& address);

    // Fails every blocked or later operation; used by session teardown.
    void Stop();

    struct Core;

private:
    std::shared_ptr<Core> core_;
};

}  // namespace ogplay::runtime::net
