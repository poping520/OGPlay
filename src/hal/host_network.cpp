#include "ogplay/hal/host_network.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <mutex>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ogplay::hal {
namespace {

// Linux errno values: the guest ABI the callers forward unchanged.
constexpr int kLinuxEintr = 4;
constexpr int kLinuxEio = 5;
constexpr int kLinuxEbadf = 9;
constexpr int kLinuxEagain = 11;
constexpr int kLinuxEnomem = 12;
constexpr int kLinuxEacces = 13;
constexpr int kLinuxEfault = 14;
constexpr int kLinuxEinval = 22;
constexpr int kLinuxEmfile = 24;
constexpr int kLinuxEpipe = 32;
constexpr int kLinuxEmsgsize = 90;
constexpr int kLinuxEnoprotoopt = 92;
constexpr int kLinuxEopnotsupp = 95;
constexpr int kLinuxEafnosupport = 97;
constexpr int kLinuxEaddrinuse = 98;
constexpr int kLinuxEaddrnotavail = 99;
constexpr int kLinuxEnetdown = 100;
constexpr int kLinuxEnetunreach = 101;
constexpr int kLinuxEnetreset = 102;
constexpr int kLinuxEconnaborted = 103;
constexpr int kLinuxEconnreset = 104;
constexpr int kLinuxEnobufs = 105;
constexpr int kLinuxEisconn = 106;
constexpr int kLinuxEnotconn = 107;
constexpr int kLinuxEshutdown = 108;
constexpr int kLinuxEtimedout = 110;
constexpr int kLinuxEconnrefused = 111;
constexpr int kLinuxEhostdown = 112;
constexpr int kLinuxEhostunreach = 113;
constexpr int kLinuxEalready = 114;
constexpr int kLinuxEinprogress = 115;

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
using AddressLength = int;

int LastError() {
    return WSAGetLastError();
}

int ToLinux(const int error) {
    switch (error) {
    case WSAEINTR: return kLinuxEintr;
    case WSAEBADF:
    case WSAENOTSOCK: return kLinuxEbadf;
    case WSAEACCES: return kLinuxEacces;
    case WSAEFAULT: return kLinuxEfault;
    case WSAEINVAL: return kLinuxEinval;
    case WSAEMFILE: return kLinuxEmfile;
    case WSAEWOULDBLOCK: return kLinuxEagain;
    case WSAEINPROGRESS: return kLinuxEinprogress;
    case WSAEALREADY: return kLinuxEalready;
    case WSAEMSGSIZE: return kLinuxEmsgsize;
    case WSAENOPROTOOPT: return kLinuxEnoprotoopt;
    case WSAEOPNOTSUPP: return kLinuxEopnotsupp;
    case WSAEAFNOSUPPORT: return kLinuxEafnosupport;
    case WSAEADDRINUSE: return kLinuxEaddrinuse;
    case WSAEADDRNOTAVAIL: return kLinuxEaddrnotavail;
    case WSAENETDOWN: return kLinuxEnetdown;
    case WSAENETUNREACH: return kLinuxEnetunreach;
    case WSAENETRESET: return kLinuxEnetreset;
    case WSAECONNABORTED: return kLinuxEconnaborted;
    case WSAECONNRESET: return kLinuxEconnreset;
    case WSAENOBUFS: return kLinuxEnobufs;
    case WSAEISCONN: return kLinuxEisconn;
    case WSAENOTCONN: return kLinuxEnotconn;
    case WSAESHUTDOWN: return kLinuxEshutdown;
    case WSAETIMEDOUT: return kLinuxEtimedout;
    case WSAECONNREFUSED: return kLinuxEconnrefused;
    case WSAEHOSTDOWN: return kLinuxEhostdown;
    case WSAEHOSTUNREACH: return kLinuxEhostunreach;
    default: return kLinuxEio;
    }
}

void CloseNative(const NativeSocket socket) {
    closesocket(socket);
}

bool SetNonBlockingNative(const NativeSocket socket) {
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
}

class WinsockSession final {
public:
    WinsockSession() {
        WSADATA data{};
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockSession() {
        if (ready_) WSACleanup();
    }

    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;

    [[nodiscard]] bool Ready() const { return ready_; }

private:
    bool ready_{};
};
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
using AddressLength = socklen_t;

int LastError() {
    return errno;
}

int ToLinux(const int error) {
    // if-chain, not switch: hosts alias some names (EWOULDBLOCK == EAGAIN).
    if (error == EINTR) return kLinuxEintr;
    if (error == EBADF || error == ENOTSOCK) return kLinuxEbadf;
    if (error == EAGAIN || error == EWOULDBLOCK) return kLinuxEagain;
    if (error == ENOMEM) return kLinuxEnomem;
    if (error == EACCES || error == EPERM) return kLinuxEacces;
    if (error == EFAULT) return kLinuxEfault;
    if (error == EINVAL) return kLinuxEinval;
    if (error == EMFILE || error == ENFILE) return kLinuxEmfile;
    if (error == EPIPE) return kLinuxEpipe;
    if (error == EMSGSIZE) return kLinuxEmsgsize;
    if (error == ENOPROTOOPT) return kLinuxEnoprotoopt;
    if (error == EOPNOTSUPP) return kLinuxEopnotsupp;
    if (error == EAFNOSUPPORT) return kLinuxEafnosupport;
    if (error == EADDRINUSE) return kLinuxEaddrinuse;
    if (error == EADDRNOTAVAIL) return kLinuxEaddrnotavail;
    if (error == ENETDOWN) return kLinuxEnetdown;
    if (error == ENETUNREACH) return kLinuxEnetunreach;
    if (error == ENETRESET) return kLinuxEnetreset;
    if (error == ECONNABORTED) return kLinuxEconnaborted;
    if (error == ECONNRESET) return kLinuxEconnreset;
    if (error == ENOBUFS) return kLinuxEnobufs;
    if (error == EISCONN) return kLinuxEisconn;
    if (error == ENOTCONN) return kLinuxEnotconn;
    if (error == ESHUTDOWN) return kLinuxEshutdown;
    if (error == ETIMEDOUT) return kLinuxEtimedout;
    if (error == ECONNREFUSED) return kLinuxEconnrefused;
    if (error == EHOSTDOWN) return kLinuxEhostdown;
    if (error == EHOSTUNREACH) return kLinuxEhostunreach;
    if (error == EALREADY) return kLinuxEalready;
    if (error == EINPROGRESS) return kLinuxEinprogress;
    return kLinuxEio;
}

void CloseNative(const NativeSocket socket) {
    ::close(socket);
}

bool SetNonBlockingNative(const NativeSocket socket) {
    const auto flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0 && fcntl(socket, F_SETFD, FD_CLOEXEC) == 0;
}
#endif

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

int Failure() {
    return -ToLinux(LastError());
}

std::size_t ToNative(const HostSocketAddress& address, sockaddr_storage& storage) {
    std::memset(&storage, 0, sizeof(storage));
    if (address.family == HostAddressFamily::inet) {
        auto* inet = reinterpret_cast<sockaddr_in*>(&storage);
        inet->sin_family = AF_INET;
        inet->sin_port = htons(address.port);
        std::memcpy(&inet->sin_addr, address.address.data(), 4);
        return sizeof(sockaddr_in);
    }
    auto* inet6 = reinterpret_cast<sockaddr_in6*>(&storage);
    inet6->sin6_family = AF_INET6;
    inet6->sin6_port = htons(address.port);
    std::memcpy(&inet6->sin6_addr, address.address.data(), 16);
    inet6->sin6_scope_id = address.scope_id;
    return sizeof(sockaddr_in6);
}

std::optional<HostSocketAddress> FromNative(const sockaddr* address) {
    HostSocketAddress result;
    if (address->sa_family == AF_INET) {
        const auto* inet = reinterpret_cast<const sockaddr_in*>(address);
        result.family = HostAddressFamily::inet;
        result.port = ntohs(inet->sin_port);
        std::memcpy(result.address.data(), &inet->sin_addr, 4);
        return result;
    }
    if (address->sa_family == AF_INET6) {
        const auto* inet6 = reinterpret_cast<const sockaddr_in6*>(address);
        result.family = HostAddressFamily::inet6;
        result.port = ntohs(inet6->sin6_port);
        std::memcpy(result.address.data(), &inet6->sin6_addr, 16);
        result.scope_id = inet6->sin6_scope_id;
        return result;
    }
    return std::nullopt;
}

#if defined(_WIN32)
using TransferLength = int;
#else
using TransferLength = std::size_t;
#endif

TransferLength ClampSize(const std::size_t size) {
    return static_cast<TransferLength>(
        std::min<std::size_t>(size, static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

class SystemSocket final : public HostSocket {
public:
    explicit SystemSocket(const NativeSocket socket) : socket_(socket) {}

    ~SystemSocket() override { CloseNative(socket_); }

    SystemSocket(const SystemSocket&) = delete;
    SystemSocket& operator=(const SystemSocket&) = delete;

    int Connect(const HostSocketAddress& address) override {
        sockaddr_storage storage{};
        const auto length = ToNative(address, storage);
        if (::connect(socket_, reinterpret_cast<const sockaddr*>(&storage), static_cast<AddressLength>(length)) == 0)
            return 0;
        const auto error = ToLinux(LastError());
        // Winsock reports a pending non-blocking connect as WSAEWOULDBLOCK.
        if (error == kLinuxEagain) return -kLinuxEinprogress;
        return -error;
    }

    int TakeError() override {
        int value{};
        AddressLength length = sizeof(value);
        if (::getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&value), &length) != 0)
            return Failure();
        return value == 0 ? 0 : -ToLinux(value);
    }

    int Bind(const HostSocketAddress& address) override {
        sockaddr_storage storage{};
        const auto length = ToNative(address, storage);
        return ::bind(socket_, reinterpret_cast<const sockaddr*>(&storage), static_cast<AddressLength>(length)) == 0
                   ? 0
                   : Failure();
    }

    std::int64_t Send(const std::span<const std::byte> bytes) override {
        const auto sent =
            ::send(socket_, reinterpret_cast<const char*>(bytes.data()), ClampSize(bytes.size()), kSendFlags);
        return sent < 0 ? Failure() : static_cast<std::int64_t>(sent);
    }

    std::int64_t SendTo(const std::span<const std::byte> bytes, const HostSocketAddress& to) override {
        sockaddr_storage storage{};
        const auto length = ToNative(to, storage);
        const auto sent =
            ::sendto(socket_, reinterpret_cast<const char*>(bytes.data()), ClampSize(bytes.size()), kSendFlags,
                     reinterpret_cast<const sockaddr*>(&storage), static_cast<AddressLength>(length));
        return sent < 0 ? Failure() : static_cast<std::int64_t>(sent);
    }

    std::int64_t Receive(const std::span<std::byte> bytes, const bool peek) override {
        const auto received =
            ::recv(socket_, reinterpret_cast<char*>(bytes.data()), ClampSize(bytes.size()), peek ? MSG_PEEK : 0);
        return received < 0 ? Failure() : static_cast<std::int64_t>(received);
    }

    std::int64_t ReceiveFrom(const std::span<std::byte> bytes, const bool peek, HostSocketAddress& from) override {
        sockaddr_storage storage{};
        AddressLength length = sizeof(storage);
        const auto received = ::recvfrom(socket_, reinterpret_cast<char*>(bytes.data()), ClampSize(bytes.size()),
                                         peek ? MSG_PEEK : 0, reinterpret_cast<sockaddr*>(&storage), &length);
#if defined(_WIN32)
        // Winsock reports a truncated datagram as WSAEMSGSIZE with the prefix copied.
        if (received < 0 && WSAGetLastError() == WSAEMSGSIZE) {
            if (const auto address = FromNative(reinterpret_cast<const sockaddr*>(&storage))) from = *address;
            return ClampSize(bytes.size());
        }
#endif
        if (received < 0) return Failure();
        if (const auto address = FromNative(reinterpret_cast<const sockaddr*>(&storage))) from = *address;
        return static_cast<std::int64_t>(received);
    }

    int Shutdown(const int how) override {
#if defined(_WIN32)
        const int native = how == 0 ? SD_RECEIVE : how == 1 ? SD_SEND : SD_BOTH;
#else
        const int native = how == 0 ? SHUT_RD : how == 1 ? SHUT_WR : SHUT_RDWR;
#endif
        return ::shutdown(socket_, native) == 0 ? 0 : Failure();
    }

    int LocalAddress(HostSocketAddress& address) override { return Name(address, false); }

    int PeerAddress(HostSocketAddress& address) override { return Name(address, true); }

    int SetOption(const HostSocketOption option, const int value) override {
        int level = SOL_SOCKET;
        int name{};
        switch (option) {
        case HostSocketOption::no_delay:
            level = IPPROTO_TCP;
            name = TCP_NODELAY;
            break;
        case HostSocketOption::keep_alive: name = SO_KEEPALIVE; break;
        case HostSocketOption::receive_buffer: name = SO_RCVBUF; break;
        case HostSocketOption::send_buffer: name = SO_SNDBUF; break;
        case HostSocketOption::broadcast: name = SO_BROADCAST; break;
        case HostSocketOption::reuse_address: name = SO_REUSEADDR; break;
        case HostSocketOption::ttl:
            level = IPPROTO_IP;
            name = IP_TTL;
            break;
        }
        return ::setsockopt(socket_, level, name, reinterpret_cast<const char*>(&value), sizeof(value)) == 0
                   ? 0
                   : Failure();
    }

    std::optional<std::size_t> Available() override {
#if defined(_WIN32)
        u_long count{};
        if (ioctlsocket(socket_, FIONREAD, &count) != 0) return std::nullopt;
#else
        int count{};
        if (::ioctl(socket_, FIONREAD, &count) != 0 || count < 0) return std::nullopt;
#endif
        return static_cast<std::size_t>(count);
    }

    HostReadiness Wait(const bool read, const bool write, const std::chrono::milliseconds timeout) override {
#if defined(_WIN32)
        WSAPOLLFD entry{};
        entry.fd = socket_;
        entry.events = static_cast<SHORT>((read ? POLLRDNORM : 0) | (write ? POLLWRNORM : 0));
        const auto result = WSAPoll(&entry, 1, static_cast<INT>(timeout.count()));
        HostReadiness readiness;
        if (result <= 0) return readiness;
        readiness.readable = (entry.revents & (POLLRDNORM | POLLRDBAND)) != 0;
        readiness.writable = (entry.revents & POLLWRNORM) != 0;
        readiness.error = (entry.revents & (POLLERR | POLLNVAL)) != 0;
        readiness.hangup = (entry.revents & POLLHUP) != 0;
        return readiness;
#else
        pollfd entry{};
        entry.fd = socket_;
        entry.events = static_cast<short>((read ? POLLIN : 0) | (write ? POLLOUT : 0));
        const auto result = ::poll(&entry, 1, static_cast<int>(timeout.count()));
        HostReadiness readiness;
        if (result <= 0) return readiness;
        readiness.readable = (entry.revents & POLLIN) != 0;
        readiness.writable = (entry.revents & POLLOUT) != 0;
        readiness.error = (entry.revents & (POLLERR | POLLNVAL)) != 0;
        readiness.hangup = (entry.revents & POLLHUP) != 0;
        return readiness;
#endif
    }

private:
    int Name(HostSocketAddress& address, const bool peer) {
        sockaddr_storage storage{};
        AddressLength length = sizeof(storage);
        const auto result = peer ? ::getpeername(socket_, reinterpret_cast<sockaddr*>(&storage), &length)
                                 : ::getsockname(socket_, reinterpret_cast<sockaddr*>(&storage), &length);
        if (result != 0) return Failure();
        const auto converted = FromNative(reinterpret_cast<const sockaddr*>(&storage));
        if (!converted) return -kLinuxEafnosupport;
        address = *converted;
        return 0;
    }

    NativeSocket socket_;
};

class SystemNetwork final : public HostNetwork {
public:
    std::unique_ptr<HostSocket> Open(const HostAddressFamily family, const HostSocketType type, int* error) override {
#if defined(_WIN32)
        if (!session_.Ready()) {
            if (error != nullptr) *error = -kLinuxEnetdown;
            return nullptr;
        }
#endif
        const int domain = family == HostAddressFamily::inet ? AF_INET : AF_INET6;
        const int kind = type == HostSocketType::stream ? SOCK_STREAM : SOCK_DGRAM;
        const auto socket = ::socket(domain, kind, 0);
        if (socket == kInvalidSocket) {
            if (error != nullptr) *error = Failure();
            return nullptr;
        }
        if (!SetNonBlockingNative(socket)) {
            const auto failure = Failure();
            CloseNative(socket);
            if (error != nullptr) *error = failure;
            return nullptr;
        }
#if defined(SO_NOSIGPIPE)
        const int enabled = 1;
        ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
        if (error != nullptr) *error = 0;
        return std::make_unique<SystemSocket>(socket);
    }

    HostResolveResult Resolve(const std::string_view name) override {
        HostResolveResult result;
#if defined(_WIN32)
        if (!session_.Ready()) return result;
#endif
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_CANONNAME;
        addrinfo* list{};
        const std::string text(name);
        if (::getaddrinfo(text.c_str(), nullptr, &hints, &list) != 0 || list == nullptr) return result;
        for (auto* entry = list; entry != nullptr; entry = entry->ai_next) {
            if (entry->ai_canonname != nullptr && result.canonical_name.empty())
                result.canonical_name = entry->ai_canonname;
            if (entry->ai_addr == nullptr) continue;
            if (auto address = FromNative(entry->ai_addr)) {
                address->port = 0;
                if (std::find(result.addresses.begin(), result.addresses.end(), *address) == result.addresses.end())
                    result.addresses.push_back(*address);
            }
        }
        ::freeaddrinfo(list);
        result.found = !result.addresses.empty();
        return result;
    }

    std::optional<std::string> ReverseLookup(const HostSocketAddress& address) override {
        sockaddr_storage storage{};
        const auto length = ToNative(address, storage);
        char host[NI_MAXHOST]{};
        if (::getnameinfo(reinterpret_cast<const sockaddr*>(&storage), static_cast<AddressLength>(length), host,
                          sizeof(host), nullptr, 0, NI_NAMEREQD) != 0)
            return std::nullopt;
        return std::string(host);
    }

private:
#if defined(_WIN32)
    WinsockSession session_;
#endif
};

}  // namespace

std::unique_ptr<HostNetwork> CreateSystemHostNetwork() {
    return std::make_unique<SystemNetwork>();
}

}  // namespace ogplay::hal
