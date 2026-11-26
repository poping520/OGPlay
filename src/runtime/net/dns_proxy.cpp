// Server side of the API19 netd DnsProxyListener protocol, matching the
// bionic clients in libc/netbsd/net/getaddrinfo.c (android_getaddrinfo_proxy)
// and libc/netbsd/gethnamaddr.c (android_read_hostent).

#include <algorithm>
#include <charconv>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

#include "net_internal.h"

namespace ogplay::runtime::net {
namespace {

// ResponseCode::DnsProxyQueryResult / DnsProxyOperationFailed, sent by
// SocketClient::sendCode as three ASCII digits plus NUL.
constexpr std::string_view kQueryResult{"222", 4};
constexpr std::string_view kOperationFailed{"401", 4};

// bionic netdb.h.
constexpr int kAiCanonName = 0x2;
constexpr int kAiNumericServ = 0x8;
constexpr int kAiAddrConfig = 0x400;
constexpr int kSockRaw = 3;
constexpr std::int32_t kEaiNoData = 7;

void AppendBigEndian32(std::vector<std::byte>& out, const std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
}

void AppendLittle32(std::vector<std::byte>& out, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
}

void AppendText(std::vector<std::byte>& out, const std::string_view text) {
    for (const auto value : text)
        out.push_back(static_cast<std::byte>(value));
}

// netd sendLenAndData: big-endian length, then the bytes.
void AppendLengthAndData(std::vector<std::byte>& out, const std::span<const std::byte> data) {
    AppendBigEndian32(out, static_cast<std::uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

std::vector<std::byte> Failure() {
    std::vector<std::byte> out;
    AppendText(out, kOperationFailed);
    // sendBinaryMsg payload: the getaddrinfo error. Bionic reads and ignores it.
    AppendBigEndian32(out, kEaiNoData);
    return out;
}

std::vector<std::string_view> Split(std::string_view text) {
    std::vector<std::string_view> tokens;
    while (!text.empty()) {
        const auto space = text.find(' ');
        tokens.push_back(text.substr(0, space));
        if (space == std::string_view::npos) break;
        text.remove_prefix(space + 1);
    }
    return tokens;
}

std::optional<int> Integer(const std::string_view text) {
    int value{};
    const auto [last, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || last != text.data() + text.size()) return std::nullopt;
    return value;
}

// API19 bionic /system/etc/services subset that games name instead of a port.
std::optional<std::uint16_t> ServicePort(const std::string_view service, const int flags) {
    if (const auto number = Integer(service)) {
        if (*number < 0 || *number > 65535) return std::nullopt;
        return static_cast<std::uint16_t>(*number);
    }
    if ((flags & kAiNumericServ) != 0) return std::nullopt;
    constexpr std::pair<std::string_view, std::uint16_t> kServices[] = {
        {"ftp", 21},   {"ssh", 22},  {"telnet", 23}, {"smtp", 25},   {"domain", 53}, {"http", 80},   {"www", 80},
        {"pop3", 110}, {"ntp", 123}, {"imap", 143},  {"https", 443}, {"imaps", 993}, {"pop3s", 995},
    };
    for (const auto& [name, port] : kServices)
        if (name == service) return port;
    return std::nullopt;
}

std::vector<std::byte> AddrinfoResponse(GuestNetworkStack::Core& core, const std::vector<std::string_view>& args) {
    // getaddrinfo <host> <service> <flags> <family> <socktype> <protocol> <iface>
    if (args.size() < 8) return Failure();
    const auto host = args[1];
    const auto service = args[2];
    const auto flags = Integer(args[3]).value_or(-1);
    const auto family = Integer(args[4]).value_or(-1);
    const auto socktype = Integer(args[5]).value_or(-1);
    const auto protocol = Integer(args[6]).value_or(-1);
    if (host == "^") return Failure();
    std::uint16_t port{};
    if (service != "^") {
        const auto resolved = ServicePort(service, flags < 0 ? 0 : flags);
        if (!resolved) return Failure();
        port = *resolved;
    }
    const auto result = ResolveName(core, host);
    if (!result.found) return Failure();

    // bionic explore[] order: per family, SOCK_DGRAM, SOCK_STREAM, SOCK_RAW.
    struct Kind final {
        int socktype;
        int protocol;
    };

    constexpr Kind kKinds[] = {{kSockDgram, kIpprotoUdp}, {kSockStream, kIpprotoTcp}, {kSockRaw, 0}};
    const bool numeric = ParseNumericAddress(host).has_value();
    const bool loopback_name = !result.addresses.empty() && IsLoopback(result.addresses.front());
    std::vector<std::byte> out;
    AppendText(out, kQueryResult);
    bool any = false;
    bool first = true;
    for (const int wanted_family : {kAfInet6, kAfInet}) {
        if (family > 0 && family != wanted_family) continue;
        // AI_ADDRCONFIG: the emulated device has an IPv4 address only online.
        if (flags > 0 && (flags & kAiAddrConfig) != 0 && !numeric && !loopback_name &&
            (wanted_family == kAfInet6 || core.config.mode != NetworkMode::online))
            continue;
        for (const auto& kind : kKinds) {
            if (socktype > 0 && socktype != kind.socktype) continue;
            if (socktype <= 0 && kind.socktype == kSockRaw && service != "^") continue;
            if (protocol > 0 && kind.protocol != 0 && protocol != kind.protocol) continue;
            for (auto address : result.addresses) {
                if (address.family != wanted_family) continue;
                address.port = port;
                const auto sockaddr = EncodeSockaddr(address);
                // 32-bit ARM struct addrinfo; pointer fields are zero, the client rebuilds them.
                std::vector<std::byte> info;
                AppendLittle32(info, static_cast<std::uint32_t>(flags < 0 ? 0 : flags));
                AppendLittle32(info, static_cast<std::uint32_t>(wanted_family));
                AppendLittle32(info, static_cast<std::uint32_t>(kind.socktype));
                AppendLittle32(info, static_cast<std::uint32_t>(protocol > 0 ? protocol : kind.protocol));
                AppendLittle32(info, static_cast<std::uint32_t>(sockaddr.size()));
                AppendLittle32(info, 0);
                AppendLittle32(info, 0);
                AppendLittle32(info, 0);
                AppendLengthAndData(out, info);
                AppendLengthAndData(out, sockaddr);
                if (first && flags > 0 && (flags & kAiCanonName) != 0) {
                    std::vector<std::byte> name;
                    AppendText(name, result.canonical_name);
                    name.push_back(std::byte{0});
                    AppendLengthAndData(out, name);
                } else {
                    AppendBigEndian32(out, 0);
                }
                first = false;
                any = true;
            }
        }
    }
    if (!any) return Failure();
    AppendBigEndian32(out, 0);
    return out;
}

std::vector<std::byte> HostentResponse(const std::string& name, const std::vector<GuestSocketAddress>& addresses,
                                       const int family) {
    std::vector<std::byte> out;
    AppendText(out, kQueryResult);
    std::vector<std::byte> name_bytes;
    AppendText(name_bytes, name);
    name_bytes.push_back(std::byte{0});
    AppendLengthAndData(out, name_bytes);
    AppendBigEndian32(out, 0);  // No aliases.
    AppendBigEndian32(out, static_cast<std::uint32_t>(family));
    const auto length = family == kAfInet ? 4U : 16U;
    AppendBigEndian32(out, length);
    for (const auto& address : addresses) {
        std::vector<std::byte> bytes;
        for (std::size_t index = 0; index < length; ++index)
            bytes.push_back(static_cast<std::byte>(address.address[index]));
        AppendLengthAndData(out, bytes);
    }
    AppendBigEndian32(out, 0);
    return out;
}

std::vector<std::byte> HostByNameResponse(GuestNetworkStack::Core& core, const std::vector<std::string_view>& args) {
    // gethostbyname <iface> <name> <af>
    if (args.size() < 4 || args[2] == "^") return Failure();
    const auto family = Integer(args[3]).value_or(kAfInet);
    if (family != kAfInet && family != kAfInet6) return Failure();
    const auto result = ResolveName(core, args[2]);
    if (!result.found) return Failure();
    std::vector<GuestSocketAddress> matching;
    for (const auto& address : result.addresses)
        if (address.family == family) matching.push_back(address);
    if (matching.empty()) return Failure();
    return HostentResponse(result.canonical_name.empty() ? std::string(args[2]) : result.canonical_name, matching,
                           family);
}

std::vector<std::byte> HostByAddressResponse(GuestNetworkStack::Core& core, const std::vector<std::string_view>& args) {
    // gethostbyaddr <address> <length> <af> <iface>
    if (args.size() < 4) return Failure();
    const auto address = ParseNumericAddress(args[1]);
    if (!address) return Failure();
    std::optional<std::string> name;
    if (IsLoopback(*address)) {
        name = address->family == kAfInet ? "localhost" : "ip6-localhost";
    } else if (core.config.mode == NetworkMode::online && core.config.host != nullptr) {
        hal::HostSocketAddress host;
        host.family = address->family == kAfInet ? hal::HostAddressFamily::inet : hal::HostAddressFamily::inet6;
        host.address = address->address;
        name = core.config.host->ReverseLookup(host);
    }
    if (!name) return Failure();
    return HostentResponse(*name, {*address}, address->family);
}

}  // namespace

bool DnsProxySession::Append(const std::span<const std::byte> bytes) {
    if (complete_) return true;
    for (const auto value : bytes) {
        if (value == std::byte{0}) {
            complete_ = true;
            return true;
        }
        // netd's FrameworkListener buffer is 4096 bytes; longer commands are dropped.
        if (request_.size() >= 4096) {
            complete_ = true;
            request_.clear();
            return true;
        }
        request_.push_back(static_cast<char>(value));
    }
    return false;
}

std::vector<std::byte> DnsProxySession::Respond(GuestNetworkStack::Core& core) const {
    const auto args = Split(request_);
    if (args.empty()) return Failure();
    if (args[0] == "getaddrinfo") return AddrinfoResponse(core, args);
    if (args[0] == "gethostbyname") return HostByNameResponse(core, args);
    if (args[0] == "gethostbyaddr") return HostByAddressResponse(core, args);
    return Failure();
}

}  // namespace ogplay::runtime::net
