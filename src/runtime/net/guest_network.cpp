#include "ogplay/runtime/net/guest_network.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <limits>

#include "net_internal.h"
#include "ogplay/core/logger.h"

namespace ogplay::runtime::net {
namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kWaitSlice = std::chrono::milliseconds(10);
constexpr std::uint16_t kEphemeralFirst = 32768;
constexpr std::uint16_t kEphemeralLast = 60999;

// Linux SOL_SOCKET option names.
constexpr int kSoReuseAddr = 2;
constexpr int kSoType = 3;
constexpr int kSoError = 4;
constexpr int kSoDontRoute = 5;
constexpr int kSoBroadcast = 6;
constexpr int kSoSndBuf = 7;
constexpr int kSoRcvBuf = 8;
constexpr int kSoKeepAlive = 9;
constexpr int kSoOobInline = 10;
constexpr int kSoLinger = 13;
constexpr int kSoReusePort = 15;
constexpr int kSoPeerCred = 17;
constexpr int kSoRcvLowat = 18;
constexpr int kSoSndLowat = 19;
constexpr int kSoRcvTimeo = 20;
constexpr int kSoSndTimeo = 21;
constexpr int kSoAcceptConn = 30;
constexpr int kSoProtocol = 38;
constexpr int kSoDomain = 39;
constexpr int kTcpNoDelay = 1;
constexpr int kIpTtl = 2;
constexpr int kIpv6V6Only = 26;

std::uint16_t Load16(const std::span<const std::byte> bytes, const std::size_t offset) {
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[offset]) |
                                      (std::to_integer<unsigned>(bytes[offset + 1]) << 8U));
}

std::uint32_t Load32(const std::span<const std::byte> bytes, const std::size_t offset) {
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index)
        value |= std::to_integer<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    return value;
}

void Store16(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>(value >> 8U));
}

void Store32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
}

std::vector<std::byte> IntValue(const std::int32_t value) {
    std::vector<std::byte> bytes;
    Store32(bytes, static_cast<std::uint32_t>(value));
    return bytes;
}

bool IsV4Mapped(const GuestSocketAddress& address) {
    if (address.family != kAfInet6) return false;
    for (std::size_t index = 0; index < 10; ++index)
        if (address.address[index] != 0) return false;
    return address.address[10] == 0xff && address.address[11] == 0xff;
}

bool IsAny(const GuestSocketAddress& address) {
    if (IsV4Mapped(address))
        return address.address[12] == 0 && address.address[13] == 0 && address.address[14] == 0 &&
               address.address[15] == 0;
    const auto size = address.family == kAfInet ? 4U : 16U;
    for (std::size_t index = 0; index < size; ++index)
        if (address.address[index] != 0) return false;
    return true;
}

// IPv4 view of an inet address: AF_INET itself or an IPv4-mapped IPv6 address.
std::optional<std::array<std::uint8_t, 4>> Ipv4Of(const GuestSocketAddress& address) {
    if (address.family == kAfInet)
        return std::array{address.address[0], address.address[1], address.address[2], address.address[3]};
    if (IsV4Mapped(address))
        return std::array{address.address[12], address.address[13], address.address[14], address.address[15]};
    return std::nullopt;
}

hal::HostSocketAddress ToHost(const GuestSocketAddress& address) {
    hal::HostSocketAddress host;
    host.port = address.port;
    if (const auto ipv4 = Ipv4Of(address)) {
        host.family = hal::HostAddressFamily::inet;
        std::copy(ipv4->begin(), ipv4->end(), host.address.begin());
    } else {
        host.family = hal::HostAddressFamily::inet6;
        host.address = address.address;
        host.scope_id = address.scope_id;
    }
    return host;
}

// Converts a host address to the guest socket's family (IPv4-mapped for AF_INET6).
GuestSocketAddress FromHost(const hal::HostSocketAddress& host, const int family) {
    GuestSocketAddress address;
    address.port = host.port;
    if (host.family == hal::HostAddressFamily::inet) {
        if (family == kAfInet6) {
            address.family = kAfInet6;
            address.address[10] = 0xff;
            address.address[11] = 0xff;
            std::copy_n(host.address.begin(), 4, address.address.begin() + 12);
        } else {
            address.family = kAfInet;
            std::copy_n(host.address.begin(), 4, address.address.begin());
        }
    } else {
        address.family = kAfInet6;
        address.address = host.address;
        address.scope_id = host.scope_id;
    }
    return address;
}

std::string UnixKey(const GuestSocketAddress& address) {
    return (address.abstract_name ? "a" : "p") + address.path;
}

class Deadline final {
public:
    explicit Deadline(const std::int64_t timeout_us) {
        if (timeout_us > 0) end_ = Clock::now() + std::chrono::microseconds(timeout_us);
    }

    [[nodiscard]] bool Expired() const { return end_ && Clock::now() >= *end_; }

    [[nodiscard]] std::chrono::milliseconds Slice() const {
        if (!end_) return kWaitSlice;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(*end_ - Clock::now());
        return std::clamp(left, std::chrono::milliseconds(1), kWaitSlice);
    }

private:
    std::optional<Clock::time_point> end_;
};

class GuestSocket;

}  // namespace

// ---- address helpers -------------------------------------------------------

GuestSocketAddress GuestSocketAddress::Inet4(const std::array<std::uint8_t, 4> bytes, const std::uint16_t port) {
    GuestSocketAddress address;
    address.family = kAfInet;
    std::copy(bytes.begin(), bytes.end(), address.address.begin());
    address.port = port;
    return address;
}

GuestSocketAddress GuestSocketAddress::Unix(std::string path, const bool abstract_name) {
    GuestSocketAddress address;
    address.family = kAfUnix;
    address.path = std::move(path);
    address.abstract_name = abstract_name;
    return address;
}

std::optional<NetworkMode> ParseNetworkMode(const std::string_view text) {
    if (text == "disabled") return NetworkMode::disabled;
    if (text == "loopback") return NetworkMode::loopback;
    if (text == "allow") return NetworkMode::online;
    return std::nullopt;
}

std::string_view NetworkModeName(const NetworkMode mode) {
    switch (mode) {
    case NetworkMode::disabled: return "disabled";
    case NetworkMode::loopback: return "loopback";
    case NetworkMode::online: return "allow";
    }
    return "disabled";
}

int DecodeSockaddr(const std::span<const std::byte> bytes, GuestSocketAddress& address) {
    if (bytes.size() < 2) return -kEinval;
    address = {};
    const auto family = Load16(bytes, 0);
    if (family == kAfUnix) {
        address.family = kAfUnix;
        const auto path = bytes.subspan(2);
        if (path.empty()) return 0;  // Unnamed (autobind is not supported).
        if (path.size() > 108) return -kEinval;
        if (path[0] == std::byte{0}) {
            address.abstract_name = true;
            address.path.assign(reinterpret_cast<const char*>(path.data() + 1), path.size() - 1);
            return 0;
        }
        const auto* begin = reinterpret_cast<const char*>(path.data());
        address.path.assign(begin, strnlen(begin, path.size()));
        return 0;
    }
    if (family == kAfInet) {
        if (bytes.size() < 16) return -kEinval;
        address.family = kAfInet;
        address.port = static_cast<std::uint16_t>((std::to_integer<unsigned>(bytes[2]) << 8U) |
                                                  std::to_integer<unsigned>(bytes[3]));
        for (std::size_t index = 0; index < 4; ++index)
            address.address[index] = std::to_integer<std::uint8_t>(bytes[4 + index]);
        return 0;
    }
    if (family == kAfInet6) {
        if (bytes.size() < 24) return -kEinval;
        address.family = kAfInet6;
        address.port = static_cast<std::uint16_t>((std::to_integer<unsigned>(bytes[2]) << 8U) |
                                                  std::to_integer<unsigned>(bytes[3]));
        address.flow_info = Load32(bytes, 4);
        for (std::size_t index = 0; index < 16; ++index)
            address.address[index] = std::to_integer<std::uint8_t>(bytes[8 + index]);
        if (bytes.size() >= 28) address.scope_id = Load32(bytes, 24);
        return 0;
    }
    if (family == kAfUnspec) {
        address.family = kAfUnspec;
        return 0;
    }
    return -kEafnosupport;
}

std::vector<std::byte> EncodeSockaddr(const GuestSocketAddress& address) {
    std::vector<std::byte> bytes;
    Store16(bytes, static_cast<std::uint16_t>(address.family));
    if (address.family == kAfUnix) {
        if (address.abstract_name) bytes.push_back(std::byte{0});
        for (const auto value : address.path)
            bytes.push_back(static_cast<std::byte>(value));
        if (!address.abstract_name && !address.path.empty()) bytes.push_back(std::byte{0});
        return bytes;
    }
    if (address.family == kAfInet) {
        bytes.push_back(static_cast<std::byte>(address.port >> 8U));
        bytes.push_back(static_cast<std::byte>(address.port & 0xffU));
        for (std::size_t index = 0; index < 4; ++index)
            bytes.push_back(static_cast<std::byte>(address.address[index]));
        bytes.resize(16);
        return bytes;
    }
    if (address.family == kAfInet6) {
        bytes.push_back(static_cast<std::byte>(address.port >> 8U));
        bytes.push_back(static_cast<std::byte>(address.port & 0xffU));
        Store32(bytes, address.flow_info);
        for (const auto value : address.address)
            bytes.push_back(static_cast<std::byte>(value));
        Store32(bytes, address.scope_id);
        return bytes;
    }
    return bytes;
}

std::optional<GuestSocketAddress> ParseNumericAddress(std::string_view text) {
    const auto ipv4 = [](const std::string_view input) -> std::optional<std::array<std::uint8_t, 4>> {
        std::array<std::uint8_t, 4> bytes{};
        std::size_t offset{};
        for (std::size_t part = 0; part < 4; ++part) {
            const auto end = part == 3 ? input.size() : input.find('.', offset);
            if (end == std::string_view::npos || end == offset || end - offset > 3) return std::nullopt;
            unsigned value{};
            const auto [last, error] = std::from_chars(input.data() + offset, input.data() + end, value);
            if (error != std::errc{} || last != input.data() + end || value > 255) return std::nullopt;
            bytes[part] = static_cast<std::uint8_t>(value);
            offset = end + 1;
        }
        if (offset != input.size() + 1) return std::nullopt;
        return bytes;
    };
    if (const auto parsed = ipv4(text)) return GuestSocketAddress::Inet4(*parsed, 0);
    if (text.size() > 2 && text.front() == '[' && text.back() == ']') text = text.substr(1, text.size() - 2);
    std::uint32_t scope{};
    if (const auto percent = text.find('%'); percent != std::string_view::npos) {
        const auto zone = text.substr(percent + 1);
        const auto [last, error] = std::from_chars(zone.data(), zone.data() + zone.size(), scope);
        if (error != std::errc{} || last != zone.data() + zone.size()) return std::nullopt;
        text = text.substr(0, percent);
    }
    if (text.find(':') == std::string_view::npos) return std::nullopt;
    std::vector<std::uint16_t> left, right;
    bool ipv4_tail = false;
    const auto parse_words = [&](std::string_view input, std::vector<std::uint16_t>& output, const bool last_part) {
        if (input.empty()) return true;
        std::size_t offset{};
        while (offset <= input.size()) {
            const auto end = input.find(':', offset);
            const auto token =
                input.substr(offset, end == std::string_view::npos ? input.size() - offset : end - offset);
            if (token.empty()) return false;
            if (token.find('.') != std::string_view::npos) {
                if (!last_part || end != std::string_view::npos) return false;
                const auto tail = ipv4(token);
                if (!tail) return false;
                output.push_back(static_cast<std::uint16_t>(((*tail)[0] << 8U) | (*tail)[1]));
                output.push_back(static_cast<std::uint16_t>(((*tail)[2] << 8U) | (*tail)[3]));
                ipv4_tail = true;
            } else {
                if (token.size() > 4) return false;
                unsigned word{};
                const auto [last, error] = std::from_chars(token.data(), token.data() + token.size(), word, 16);
                if (error != std::errc{} || last != token.data() + token.size()) return false;
                output.push_back(static_cast<std::uint16_t>(word));
            }
            if (end == std::string_view::npos) break;
            offset = end + 1;
        }
        return true;
    };
    const auto compression = text.find("::");
    if (compression != std::string_view::npos && text.find("::", compression + 1) != std::string_view::npos)
        return std::nullopt;
    if (compression == std::string_view::npos) {
        if (!parse_words(text, left, true) || left.size() != 8) return std::nullopt;
    } else {
        if (!parse_words(text.substr(0, compression), left, false) ||
            !parse_words(text.substr(compression + 2), right, true) || left.size() + right.size() > 7)
            return std::nullopt;
    }
    static_cast<void>(ipv4_tail);
    std::array<std::uint16_t, 8> words{};
    std::copy(left.begin(), left.end(), words.begin());
    std::copy(right.begin(), right.end(), words.end() - static_cast<std::ptrdiff_t>(right.size()));
    GuestSocketAddress address;
    address.family = kAfInet6;
    address.scope_id = scope;
    for (std::size_t index = 0; index < 8; ++index) {
        address.address[index * 2] = static_cast<std::uint8_t>(words[index] >> 8U);
        address.address[index * 2 + 1] = static_cast<std::uint8_t>(words[index] & 0xffU);
    }
    return address;
}

std::string FormatNumericAddress(const GuestSocketAddress& address) {
    if (address.family == kAfInet) {
        return std::to_string(address.address[0]) + "." + std::to_string(address.address[1]) + "." +
               std::to_string(address.address[2]) + "." + std::to_string(address.address[3]);
    }
    if (address.family != kAfInet6) return address.path;
    std::array<std::uint16_t, 8> words{};
    for (std::size_t index = 0; index < 8; ++index)
        words[index] = static_cast<std::uint16_t>((address.address[index * 2] << 8U) | address.address[index * 2 + 1]);
    if (IsV4Mapped(address)) {
        return "::ffff:" + std::to_string(address.address[12]) + "." + std::to_string(address.address[13]) + "." +
               std::to_string(address.address[14]) + "." + std::to_string(address.address[15]);
    }
    std::size_t best = 8, best_length = 0;
    for (std::size_t index = 0; index < 8;) {
        if (words[index] != 0) {
            ++index;
            continue;
        }
        const auto start = index;
        while (index < 8 && words[index] == 0)
            ++index;
        if (index - start > best_length && index - start >= 2) {
            best = start;
            best_length = index - start;
        }
    }
    std::string text;
    for (std::size_t index = 0; index < 8;) {
        if (index == best) {
            text += "::";
            index += best_length;
            continue;
        }
        if (!text.empty() && text.back() != ':') text += ':';
        char buffer[5]{};
        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), words[index], 16);
        text.append(buffer, converted.ptr);
        ++index;
    }
    return text;
}

bool IsLoopback(const GuestSocketAddress& address) {
    if (const auto ipv4 = Ipv4Of(address)) return (*ipv4)[0] == 127;
    if (address.family != kAfInet6) return false;
    for (std::size_t index = 0; index < 15; ++index)
        if (address.address[index] != 0) return false;
    return address.address[15] == 1;
}

// ---- resolver --------------------------------------------------------------

GuestResolveResult ResolveName(GuestNetworkStack::Core& core, const std::string_view name) {
    GuestResolveResult result;
    if (auto numeric = ParseNumericAddress(name)) {
        result.found = true;
        result.addresses.push_back(*numeric);
        result.canonical_name = std::string(name);
        return result;
    }
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (!lower.empty() && lower.back() == '.') lower.pop_back();
    // API19 /system/etc/hosts: "127.0.0.1 localhost" and "::1 ip6-localhost".
    if (lower == "localhost") {
        result.found = true;
        result.addresses.push_back(GuestSocketAddress::Inet4({127, 0, 0, 1}, 0));
        result.canonical_name = "localhost";
        return result;
    }
    if (lower == "ip6-localhost") {
        result.found = true;
        GuestSocketAddress loopback;
        loopback.family = kAfInet6;
        loopback.address[15] = 1;
        result.addresses.push_back(loopback);
        result.canonical_name = "ip6-localhost";
        return result;
    }
    if (core.config.mode != NetworkMode::online || core.config.host == nullptr || core.stopped.load() || name.empty())
        return result;
    const auto host = core.config.host->Resolve(name);
    if (core.config.logger != nullptr)
        core.config.logger->Write(core::LogLevel::info, "runtime.net", "host name lookup", {},
                                  {{"name", std::string(name)},
                                   {"found", host.found},
                                   {"addresses", static_cast<std::uint64_t>(host.addresses.size())}});
    if (!host.found) return result;
    result.found = true;
    for (const auto& address : host.addresses)
        result.addresses.push_back(
            FromHost(address, address.family == hal::HostAddressFamily::inet ? kAfInet : kAfInet6));
    result.canonical_name = host.canonical_name.empty() ? std::string(name) : host.canonical_name;
    return result;
}

namespace {

// ---- binding ---------------------------------------------------------------

bool PortInUseLocked(GuestNetworkStack::Core& core, const int type, const std::uint16_t port) {
    const auto found = core.inet_ports.find({type, port});
    return found != core.inet_ports.end() && !found->second.expired();
}

int BindInetLocked(GuestNetworkStack::Core& core, SocketState& state, const std::shared_ptr<SocketState>& self,
                   GuestSocketAddress address) {
    if (state.bound) return -kEinval;
    if (address.family != state.domain && !(state.domain == kAfInet6 && address.family == kAfInet))
        return -kEafnosupport;
    if (!IsAny(address) && !IsLoopback(address)) {
        const auto ipv4 = Ipv4Of(address);
        if (!ipv4 || *ipv4 != core.config.device_ipv4 || core.config.mode != NetworkMode::online)
            return -kEaddrnotavail;
    }
    if (address.port == 0) {
        for (std::uint32_t attempts = 0; attempts <= kEphemeralLast - kEphemeralFirst; ++attempts) {
            const auto candidate = core.next_ephemeral;
            core.next_ephemeral =
                candidate >= kEphemeralLast ? kEphemeralFirst : static_cast<std::uint16_t>(candidate + 1U);
            if (!PortInUseLocked(core, state.type, candidate)) {
                address.port = candidate;
                break;
            }
        }
        if (address.port == 0) return -kEaddrinuse;
    } else if (PortInUseLocked(core, state.type, address.port)) {
        return -kEaddrinuse;
    }
    state.bound = true;
    state.local = address;
    state.registry_key = "inet";
    core.inet_ports[{state.type, address.port}] = self;
    return 0;
}

int AutoBindLocked(GuestNetworkStack::Core& core, const std::shared_ptr<SocketState>& state) {
    if (state->bound) return 0;
    GuestSocketAddress any;
    any.family = state->domain;
    return BindInetLocked(core, *state, state, any);
}

// ---- descriptor object -----------------------------------------------------

class GuestSocket final : public VfsDescriptorObject {
public:
    GuestSocket(std::shared_ptr<GuestNetworkStack::Core> core, std::shared_ptr<SocketState> state)
        : core_(std::move(core)), state_(std::move(state)) {}

    std::size_t Read(const std::span<std::byte> destination, const VfsIoContext& context) override {
        const auto result = ReceiveBytes(destination, 0, nullptr, context);
        if (result < 0) throw VfsError(static_cast<std::int32_t>(-result), "socket read failed");
        return static_cast<std::size_t>(result);
    }

    std::size_t Write(const std::span<const std::byte> source, const VfsIoContext& context) override {
        const auto result = SendBytes(source, 0, nullptr, context);
        if (result < 0) throw VfsError(static_cast<std::int32_t>(-result), "socket write failed");
        return static_cast<std::size_t>(result);
    }

    std::uint32_t PollEvents() const override;
    std::optional<std::size_t> Available() const override;
    void OnLastClose() noexcept override;

    std::int64_t SendBytes(std::span<const std::byte> bytes, int flags, const GuestSocketAddress* destination,
                           const VfsIoContext& context);
    std::int64_t ReceiveBytes(std::span<std::byte> bytes, int flags, GuestSocketAddress* source,
                              const VfsIoContext& context);

    [[nodiscard]] const std::shared_ptr<SocketState>& State() const { return state_; }

    [[nodiscard]] const std::shared_ptr<GuestNetworkStack::Core>& Core() const { return core_; }

private:
    std::int64_t SendStream(std::span<const std::byte> bytes, bool non_blocking, const VfsIoContext& context);
    std::int64_t ReceiveStream(std::span<std::byte> bytes, int flags, bool non_blocking, const VfsIoContext& context);
    std::int64_t SendDatagram(std::span<const std::byte> bytes, const GuestSocketAddress* destination);
    std::int64_t ReceiveDatagram(std::span<std::byte> bytes, int flags, GuestSocketAddress* source, bool non_blocking,
                                 const VfsIoContext& context);

    std::shared_ptr<GuestNetworkStack::Core> core_;
    std::shared_ptr<SocketState> state_;
};

// Blocks one slice: on the host socket when there is one, otherwise on the
// core change signal. Returns 0 to retry or a negative errno.
int WaitSlice(GuestNetworkStack::Core& core, std::unique_lock<std::mutex>& lock, SocketState& state,
              const VfsIoContext& context, const Deadline& deadline, const bool read) {
    if (core.stopped.load() || state.closed) return -kEbadf;
    if (context.Interrupted()) return -kEintr;
    if (deadline.Expired()) return -kEagain;
    if (const auto host = state.host) {
        lock.unlock();
        static_cast<void>(host->Wait(read, !read, deadline.Slice()));
        lock.lock();
    } else {
        core.changed.wait_for(lock, deadline.Slice());
    }
    if (core.stopped.load() || state.closed) return -kEbadf;
    return 0;
}

void CloseChannelsLocked(SocketState& state) {
    if (state.rx) {
        state.rx->read_closed = true;
        state.rx->data.clear();
    }
    if (state.tx) state.tx->write_closed = true;
}

void UnregisterLocked(GuestNetworkStack::Core& core, SocketState& state) {
    if (state.registry_key.empty()) return;
    if (state.domain == kAfUnix) {
        if (const auto found = core.unix_names.find(state.registry_key); found != core.unix_names.end())
            core.unix_names.erase(found);
    } else {
        const auto key = std::pair{state.type, state.local.port};
        if (const auto found = core.inet_ports.find(key); found != core.inet_ports.end()) core.inet_ports.erase(found);
    }
    state.registry_key.clear();
}

void CloseStateLocked(GuestNetworkStack::Core& core, SocketState& state) {
    if (state.closed) return;
    state.closed = true;
    CloseChannelsLocked(state);
    UnregisterLocked(core, state);
    for (const auto& pending : state.pending)
        CloseStateLocked(core, *pending);
    state.pending.clear();
    state.datagrams.clear();
    state.datagram_bytes = 0;
    state.host.reset();
    state.proxy.reset();
}

std::uint32_t GuestSocket::PollEvents() const {
    std::unique_lock lock(core_->mutex);
    const auto& state = *state_;
    std::uint32_t events{};
    if (state.closed) return 8U;
    if (state.pending_error != 0) events |= 4U;
    if (state.listening) {
        if (!state.pending.empty()) events |= 1U;
        return events;
    }
    if (state.type == kSockDgram) {
        events |= 2U;
        if (!state.datagrams.empty()) events |= 1U;
        if (const auto host = state.host) {
            lock.unlock();
            const auto ready = host->Wait(true, false, std::chrono::milliseconds(0));
            if (ready.readable) events |= 1U;
            if (ready.error) events |= 4U;
        }
        return events;
    }
    if (const auto host = state.host) {
        const bool connecting = state.connecting;
        const bool shut_read = state.shut_read;
        lock.unlock();
        const auto ready = host->Wait(!connecting, true, std::chrono::milliseconds(0));
        if (ready.readable || shut_read) events |= 1U;
        if (ready.writable) events |= 2U;
        if (ready.error) events |= 4U;
        if (ready.hangup) events |= 9U;
        return events;
    }
    if (!state.connected) {
        // Linux tcp_poll: a closed (never connected) TCP socket reports POLLHUP.
        if (!state.connecting) events |= 8U;
        return events;
    }
    const bool rx_eof = state.rx && state.rx->write_closed;
    if ((state.rx && !state.rx->data.empty()) || rx_eof || state.shut_read) events |= 1U;
    const bool peer_gone = state.tx && state.tx->read_closed;
    if (peer_gone) events |= 4U;
    if (!state.shut_write && !peer_gone && state.tx && state.tx->data.size() < kLocalQueueCapacity) events |= 2U;
    if (rx_eof && (peer_gone || state.shut_write)) events |= 8U;
    return events;
}

std::optional<std::size_t> GuestSocket::Available() const {
    std::unique_lock lock(core_->mutex);
    const auto& state = *state_;
    if (state.type == kSockDgram) {
        if (!state.datagrams.empty()) return state.datagrams.front().bytes.size();
        if (const auto host = state.host) {
            lock.unlock();
            return host->Available();
        }
        return 0;
    }
    if (const auto host = state.host) {
        lock.unlock();
        return host->Available();
    }
    return state.rx ? state.rx->data.size() : 0U;
}

void GuestSocket::OnLastClose() noexcept {
    std::scoped_lock lock(core_->mutex);
    CloseStateLocked(*core_, *state_);
    core_->NotifyLocked();
}

std::int64_t GuestSocket::SendBytes(const std::span<const std::byte> bytes, const int flags,
                                    const GuestSocketAddress* destination, const VfsIoContext& context) {
    if ((flags & kMsgOob) != 0) return -kEopnotsupp;
    const bool non_blocking = context.non_blocking || (flags & kMsgDontWait) != 0;
    if (state_->type == kSockDgram) return SendDatagram(bytes, destination);
    if (destination != nullptr) {
        std::scoped_lock lock(core_->mutex);
        if (!state_->connected) return -kEopnotsupp;  // TCP sendto ignores the address once connected.
    }
    return SendStream(bytes, non_blocking, context);
}

std::int64_t GuestSocket::ReceiveBytes(const std::span<std::byte> bytes, const int flags, GuestSocketAddress* source,
                                       const VfsIoContext& context) {
    if ((flags & kMsgOob) != 0) return -kEinval;
    const bool non_blocking = context.non_blocking || (flags & kMsgDontWait) != 0;
    if (state_->type == kSockDgram) return ReceiveDatagram(bytes, flags, source, non_blocking, context);
    const auto result = ReceiveStream(bytes, flags, non_blocking, context);
    if (result >= 0 && source != nullptr) {
        std::scoped_lock lock(core_->mutex);
        if (state_->has_peer) *source = state_->peer;
    }
    return result;
}

std::int64_t GuestSocket::SendStream(const std::span<const std::byte> bytes, const bool non_blocking,
                                     const VfsIoContext& context) {
    auto& core = *core_;
    std::unique_lock lock(core.mutex);
    auto& state = *state_;
    if (state.closed) return -kEbadf;
    if (state.proxy) {
        if (state.proxy->Complete()) return -kEpipe;
        if (!state.proxy->Append(bytes)) return static_cast<std::int64_t>(bytes.size());
        const auto session = state.proxy;
        lock.unlock();
        auto response = session->Respond(core);
        lock.lock();
        if (state.closed) return -kEbadf;
        if (state.rx) {
            state.rx->data.insert(state.rx->data.end(), response.begin(), response.end());
            state.rx->write_closed = true;  // netd closes after one answer.
        }
        core.NotifyLocked();
        return static_cast<std::int64_t>(bytes.size());
    }
    if (!state.connected) return state.connecting ? -kEagain : state.domain == kAfUnix ? -kEnotconn : -kEpipe;
    if (state.shut_write) return -kEpipe;
    const Deadline deadline(state.send_timeout_us);
    std::size_t written{};
    while (written < bytes.size()) {
        if (const auto host = state.host) {
            lock.unlock();
            const auto sent = host->Send(bytes.subspan(written));
            lock.lock();
            if (sent >= 0) {
                written += static_cast<std::size_t>(sent);
                continue;
            }
            if (sent != -kEagain) return written != 0 ? static_cast<std::int64_t>(written) : sent;
        } else {
            if (!state.tx || state.tx->read_closed) return written != 0 ? static_cast<std::int64_t>(written) : -kEpipe;
            const auto room = kLocalQueueCapacity - std::min(kLocalQueueCapacity, state.tx->data.size());
            if (room != 0) {
                const auto count = std::min(room, bytes.size() - written);
                const auto chunk = bytes.subspan(written, count);
                state.tx->data.insert(state.tx->data.end(), chunk.begin(), chunk.end());
                written += count;
                core.NotifyLocked();
                continue;
            }
        }
        if (non_blocking) return written != 0 ? static_cast<std::int64_t>(written) : -kEagain;
        if (const auto waited = WaitSlice(core, lock, state, context, deadline, false); waited < 0)
            return written != 0 ? static_cast<std::int64_t>(written) : waited;
    }
    return static_cast<std::int64_t>(written);
}

std::int64_t GuestSocket::ReceiveStream(const std::span<std::byte> bytes, const int flags, const bool non_blocking,
                                        const VfsIoContext& context) {
    auto& core = *core_;
    std::unique_lock lock(core.mutex);
    auto& state = *state_;
    if (state.closed) return -kEbadf;
    if (state.listening) return -kEnotconn;
    if (!state.connected && !state.proxy) return state.connecting ? -kEagain : -kEnotconn;
    const bool peek = (flags & kMsgPeek) != 0;
    const bool wait_all = (flags & kMsgWaitAll) != 0 && !peek;
    const Deadline deadline(state.receive_timeout_us);
    std::size_t read{};
    for (;;) {
        if (bytes.empty()) return 0;
        if (state.shut_read) return static_cast<std::int64_t>(read);
        if (const auto host = state.host) {
            lock.unlock();
            const auto received = host->Receive(bytes.subspan(read), peek);
            lock.lock();
            if (received == 0) return static_cast<std::int64_t>(read);
            if (received > 0) {
                read += static_cast<std::size_t>(received);
                if (!wait_all || read == bytes.size()) return static_cast<std::int64_t>(read);
                continue;
            }
            if (received != -kEagain) return read != 0 ? static_cast<std::int64_t>(read) : received;
        } else if (state.rx) {
            auto& channel = *state.rx;
            if (!channel.data.empty()) {
                const auto count = std::min(channel.data.size(), bytes.size() - read);
                std::copy_n(channel.data.begin(), count, bytes.begin() + static_cast<std::ptrdiff_t>(read));
                if (!peek)
                    channel.data.erase(channel.data.begin(), channel.data.begin() + static_cast<std::ptrdiff_t>(count));
                read += count;
                core.NotifyLocked();
                if (!wait_all || read == bytes.size()) return static_cast<std::int64_t>(read);
                continue;
            }
            if (channel.write_closed) return static_cast<std::int64_t>(read);
        } else {
            return -kEnotconn;
        }
        if (non_blocking) return read != 0 ? static_cast<std::int64_t>(read) : -kEagain;
        if (const auto waited = WaitSlice(core, lock, state, context, deadline, true); waited < 0)
            return read != 0 ? static_cast<std::int64_t>(read) : waited;
    }
}

}  // namespace

namespace {

std::int64_t GuestSocket::SendDatagram(const std::span<const std::byte> bytes, const GuestSocketAddress* destination) {
    auto& core = *core_;
    std::unique_lock lock(core.mutex);
    auto& state = *state_;
    if (state.closed) return -kEbadf;
    if (bytes.size() > kMaxDatagram) return -kEmsgsize;
    GuestSocketAddress target;
    if (destination != nullptr && destination->family != kAfUnspec) {
        target = *destination;
    } else if (state.has_peer) {
        target = state.peer;
    } else if (const auto peer = state.pair_peer.lock()) {
        if (peer->closed) return -kEconnrefused;
        Datagram datagram{std::vector<std::byte>(bytes.begin(), bytes.end()), state.local};
        if (peer->datagram_bytes + bytes.size() > kLocalQueueCapacity) return -kEagain;
        peer->datagram_bytes += bytes.size();
        peer->datagrams.push_back(std::move(datagram));
        core.NotifyLocked();
        return static_cast<std::int64_t>(bytes.size());
    } else {
        return state.domain == kAfUnix ? -kEnotconn : -kEdestaddrreq;
    }
    if (state.domain == kAfUnix) {
        if (target.family != kAfUnix) return -kEinval;
        const auto found = core.unix_names.find(UnixKey(target));
        const auto receiver = found == core.unix_names.end() ? nullptr : found->second.lock();
        if (!receiver) return target.abstract_name ? -kEconnrefused : -kEnoent;
        if (receiver->type != kSockDgram) return -kEprototype;
        if (receiver->datagram_bytes + bytes.size() > kLocalQueueCapacity) return -kEagain;
        receiver->datagram_bytes += bytes.size();
        receiver->datagrams.push_back({std::vector<std::byte>(bytes.begin(), bytes.end()), state.local});
        core.NotifyLocked();
        return static_cast<std::int64_t>(bytes.size());
    }
    if (target.family != kAfInet && target.family != kAfInet6) return -kEafnosupport;
    if (state.domain == kAfInet && target.family != kAfInet) return -kEafnosupport;
    if (IsLoopback(target)) {
        if (const auto bound = AutoBindLocked(core, state_); bound < 0) return bound;
        const auto found = core.inet_ports.find({kSockDgram, target.port});
        if (const auto receiver = found == core.inet_ports.end() ? nullptr : found->second.lock();
            receiver && !receiver->closed && receiver->datagram_bytes + bytes.size() <= kLocalQueueCapacity) {
            auto from = state.local;
            if (IsAny(from)) {
                from = GuestSocketAddress::Inet4({127, 0, 0, 1}, state.local.port);
                if (receiver->domain == kAfInet6) from = FromHost(ToHost(from), kAfInet6);
            } else if (receiver->domain == kAfInet6 && from.family == kAfInet) {
                from = FromHost(ToHost(from), kAfInet6);
            }
            receiver->datagram_bytes += bytes.size();
            receiver->datagrams.push_back({std::vector<std::byte>(bytes.begin(), bytes.end()), from});
            core.NotifyLocked();
        }
        // No receiver: UDP silently drops the datagram.
        return static_cast<std::int64_t>(bytes.size());
    }
    if (core.config.mode != NetworkMode::online || core.config.host == nullptr) return -kEnetunreach;
    if (!state.host) {
        int error{};
        const auto family = Ipv4Of(target) ? hal::HostAddressFamily::inet : hal::HostAddressFamily::inet6;
        auto host = core.config.host->Open(family, hal::HostSocketType::datagram, &error);
        if (!host) return error != 0 ? error : -kEnetunreach;
        if (state.broadcast) static_cast<void>(host->SetOption(hal::HostSocketOption::broadcast, 1));
        state.host = std::move(host);
    }
    const auto host = state.host;
    lock.unlock();
    return host->SendTo(bytes, ToHost(target));
}

std::int64_t GuestSocket::ReceiveDatagram(const std::span<std::byte> bytes, const int flags, GuestSocketAddress* source,
                                          const bool non_blocking, const VfsIoContext& context) {
    auto& core = *core_;
    std::unique_lock lock(core.mutex);
    auto& state = *state_;
    const bool peek = (flags & kMsgPeek) != 0;
    const Deadline deadline(state.receive_timeout_us);
    for (;;) {
        if (state.closed) return -kEbadf;
        if (!state.datagrams.empty()) {
            auto& front = state.datagrams.front();
            const auto size = front.bytes.size();
            const auto count = std::min(size, bytes.size());
            std::copy_n(front.bytes.begin(), count, bytes.begin());
            if (source != nullptr) *source = front.from;
            if (!peek) {
                state.datagram_bytes -= size;
                state.datagrams.pop_front();
                core.NotifyLocked();
            }
            return static_cast<std::int64_t>((flags & kMsgTrunc) != 0 ? size : count);
        }
        if (state.shut_read) return 0;
        if (const auto host = state.host) {
            lock.unlock();
            hal::HostSocketAddress from;
            const auto received = host->ReceiveFrom(bytes, peek, from);
            lock.lock();
            if (received >= 0) {
                if (source != nullptr) *source = FromHost(from, state.domain);
                return received;
            }
            if (received != -kEagain) return received;
        }
        if (non_blocking) return -kEagain;
        if (const auto waited = WaitSlice(core, lock, state, context, deadline, true); waited < 0) return waited;
    }
}

}  // namespace

// ---- stack -----------------------------------------------------------------

namespace {

std::shared_ptr<GuestSocket> SocketOf(GuestNetworkStack::Core& core, const int descriptor, int& error) {
    std::shared_ptr<VfsDescriptorObject> object;
    try {
        object = core.vfs.DescriptorObject(descriptor);
    } catch (const VfsError& failure) {
        error = -failure.ErrorNumber();
        return nullptr;
    }
    auto socket = std::dynamic_pointer_cast<GuestSocket>(object);
    error = socket ? 0 : -kEnotsock;
    return socket;
}

VfsIoContext ContextOf(GuestNetworkStack::Core& core, const int descriptor) {
    try {
        return core.vfs.IoContext(descriptor);
    } catch (const VfsError&) {
        return {};
    }
}

int Install(GuestNetworkStack::Core& core, const std::shared_ptr<GuestNetworkStack::Core>& owner,
            std::shared_ptr<SocketState> state, const int flags) {
    try {
        const auto descriptor =
            core.vfs.OpenObject(std::make_shared<GuestSocket>(owner, std::move(state)),
                                {.read = true, .write = true, .non_blocking = (flags & kSockNonBlock) != 0});
        if ((flags & kSockCloexec) != 0) core.vfs.SetCloseOnExec(descriptor, true);
        return descriptor;
    } catch (const VfsError& error) {
        return -error.ErrorNumber();
    }
}

std::shared_ptr<SocketState> NewState(const int domain, const int type, const int protocol) {
    auto state = std::make_shared<SocketState>();
    state->domain = domain;
    state->type = type;
    state->protocol = protocol;
    state->local.family = domain;
    return state;
}

// Connects two in-process stream states.
void LinkStreamsLocked(SocketState& left, SocketState& right) {
    auto forward = std::make_shared<ByteChannel>();
    auto backward = std::make_shared<ByteChannel>();
    left.tx = forward;
    right.rx = forward;
    right.tx = backward;
    left.rx = backward;
    left.connected = right.connected = true;
}

}  // namespace

GuestNetworkStack::GuestNetworkStack(VirtualFileSystem& vfs, GuestNetworkConfig config)
    : core_(std::make_shared<Core>(vfs, config)) {}

GuestNetworkStack::~GuestNetworkStack() {
    Stop();
}

NetworkMode GuestNetworkStack::Mode() const {
    return core_->config.mode;
}

const GuestNetworkConfig& GuestNetworkStack::Config() const {
    return core_->config;
}

VirtualFileSystem& GuestNetworkStack::FileSystem() const {
    return core_->vfs;
}

void GuestNetworkStack::Stop() {
    std::scoped_lock lock(core_->mutex);
    core_->stopped = true;
    core_->NotifyLocked();
}

int GuestNetworkStack::Socket(const int domain, const int raw_type, const int protocol) {
    const auto flags = raw_type & (kSockNonBlock | kSockCloexec);
    const auto type = raw_type & ~(kSockNonBlock | kSockCloexec);
    if (domain != kAfUnix && domain != kAfInet && domain != kAfInet6) return -kEafnosupport;
    if (type != kSockStream && type != kSockDgram) return -kEsocktnosupport;
    if (domain == kAfUnix && protocol != 0) return -kEprotonosupport;
    if (domain != kAfUnix) {
        if (type == kSockStream && protocol != 0 && protocol != kIpprotoTcp) return -kEprotonosupport;
        if (type == kSockDgram && protocol != 0 && protocol != kIpprotoUdp) return -kEprotonosupport;
    }
    const auto resolved = domain == kAfUnix     ? 0
                          : protocol != 0       ? protocol
                          : type == kSockStream ? kIpprotoTcp
                                                : kIpprotoUdp;
    return Install(*core_, core_, NewState(domain, type, resolved), flags);
}

int GuestNetworkStack::SocketPair(const int domain, const int raw_type, const int protocol,
                                  std::array<int, 2>& descriptors) {
    const auto flags = raw_type & (kSockNonBlock | kSockCloexec);
    const auto type = raw_type & ~(kSockNonBlock | kSockCloexec);
    if (domain != kAfUnix) return domain == kAfInet || domain == kAfInet6 ? -kEopnotsupp : -kEafnosupport;
    if (type != kSockStream && type != kSockDgram) return -kEsocktnosupport;
    if (protocol != 0) return -kEprotonosupport;
    auto left = NewState(domain, type, 0);
    auto right = NewState(domain, type, 0);
    {
        std::scoped_lock lock(core_->mutex);
        if (type == kSockStream) {
            LinkStreamsLocked(*left, *right);
        } else {
            left->pair_peer = right;
            right->pair_peer = left;
        }
    }
    const auto first = Install(*core_, core_, left, flags);
    if (first < 0) return first;
    const auto second = Install(*core_, core_, right, flags);
    if (second < 0) {
        try {
            core_->vfs.Close(first);
        } catch (const VfsError&) {
        }
        return second;
    }
    descriptors = {first, second};
    return 0;
}

int GuestNetworkStack::Bind(const int descriptor, const GuestSocketAddress& address) {
    int error{};
    const auto socket = SocketOf(*core_, descriptor, error);
    if (!socket) return error;
    const auto& state = socket->State();
    std::scoped_lock lock(core_->mutex);
    if (state->closed) return -kEbadf;
    if (state->domain == kAfUnix) {
        if (address.family != kAfUnix) return -kEinval;
        if (state->bound) return -kEinval;
        if (address.path.empty() && !address.abstract_name) return -kEinval;
        if (!address.abstract_name && address.path == kDnsProxyPath) return -kEaddrinuse;
        const auto key = UnixKey(address);
        if (const auto found = core_->unix_names.find(key);
            found != core_->unix_names.end() && !found->second.expired())
            return -kEaddrinuse;
        state->bound = true;
        state->local = address;
        state->registry_key = key;
        core_->unix_names[key] = state;
        return 0;
    }
    return BindInetLocked(*core_, *state, state, address);
}

int GuestNetworkStack::Connect(const int descriptor, const GuestSocketAddress& address) {
    int error{};
    const auto socket = SocketOf(*core_, descriptor, error);
    if (!socket) return error;
    const auto context = ContextOf(*core_, descriptor);
    const auto& state = socket->State();
    auto& core = *core_;
    std::unique_lock lock(core.mutex);
    if (state->closed) return -kEbadf;
    if (state->listening) return -kEisconn;

    if (state->type == kSockDgram) {
        if (address.family == kAfUnspec) {
            state->has_peer = false;
            return 0;
        }
        if (state->domain == kAfUnix) {
            if (address.family != kAfUnix) return -kEinval;
            const auto found = core.unix_names.find(UnixKey(address));
            const auto receiver = found == core.unix_names.end() ? nullptr : found->second.lock();
            if (!receiver) return address.abstract_name ? -kEconnrefused : -kEnoent;
            if (receiver->type != kSockDgram) return -kEprototype;
        } else {
            if (address.family != kAfInet && address.family != kAfInet6) return -kEafnosupport;
            if (!IsLoopback(address) && core.config.mode != NetworkMode::online) return -kEnetunreach;
            if (const auto bound = AutoBindLocked(core, state); bound < 0) return bound;
        }
        state->peer = address;
        state->has_peer = true;
        return 0;
    }

    if (state->connected) return -kEisconn;
    if (state->connecting) {
        if (!state->host) return -kEalready;
        const auto host = state->host;
        lock.unlock();
        const auto ready = host->Wait(false, true, std::chrono::milliseconds(0));
        if (!ready.writable && !ready.error) return -kEalready;
        const auto result = host->TakeError();
        lock.lock();
        state->connecting = false;
        if (result != 0) return result;
        state->connected = true;
        return -kEisconn;
    }

    if (state->domain == kAfUnix) {
        if (address.family != kAfUnix) return -kEinval;
        if (!address.abstract_name && address.path == kDnsProxyPath) {
            state->proxy = std::make_shared<DnsProxySession>();
            state->rx = std::make_shared<ByteChannel>();
            state->connected = true;
            state->has_peer = true;
            state->peer = address;
            core.NotifyLocked();
            return 0;
        }
        const auto found = core.unix_names.find(UnixKey(address));
        const auto listener = found == core.unix_names.end() ? nullptr : found->second.lock();
        if (!listener) return address.abstract_name ? -kEconnrefused : -kEnoent;
        if (listener->type != kSockStream) return -kEprototype;
        if (!listener->listening || listener->closed) return -kEconnrefused;
        auto accepted = NewState(kAfUnix, kSockStream, 0);
        accepted->local = listener->local;
        accepted->bound = true;
        LinkStreamsLocked(*state, *accepted);
        accepted->has_peer = true;
        accepted->peer = state->local;
        state->has_peer = true;
        state->peer = address;
        listener->pending.push_back(std::move(accepted));
        core.NotifyLocked();
        return 0;
    }

    if (address.family != kAfInet && address.family != kAfInet6) return -kEafnosupport;
    if (state->domain == kAfInet && address.family != kAfInet) return -kEafnosupport;
    if (address.port == 0 && IsLoopback(address)) return -kEconnrefused;
    if (IsLoopback(address)) {
        const auto found = core.inet_ports.find({kSockStream, address.port});
        const auto listener = found == core.inet_ports.end() ? nullptr : found->second.lock();
        if (!listener || !listener->listening || listener->closed) return -kEconnrefused;
        if (const auto bound = AutoBindLocked(core, state); bound < 0) return bound;
        if (IsAny(state->local)) {
            auto local = GuestSocketAddress::Inet4({127, 0, 0, 1}, state->local.port);
            state->local = state->domain == kAfInet6 ? FromHost(ToHost(local), kAfInet6) : local;
        }
        auto accepted = NewState(listener->domain, kSockStream, kIpprotoTcp);
        accepted->bound = true;
        accepted->local =
            listener->domain == kAfInet6 ? FromHost(ToHost(address), kAfInet6) : FromHost(ToHost(address), kAfInet);
        LinkStreamsLocked(*state, *accepted);
        accepted->has_peer = true;
        accepted->peer = listener->domain == kAfInet6 ? FromHost(ToHost(state->local), kAfInet6)
                                                      : FromHost(ToHost(state->local), kAfInet);
        state->has_peer = true;
        state->peer = address;
        accepted->no_delay = listener->no_delay;
        listener->pending.push_back(std::move(accepted));
        core.NotifyLocked();
        return 0;
    }
    if (core.config.mode != NetworkMode::online || core.config.host == nullptr) return -kEnetunreach;
    const auto family = Ipv4Of(address) ? hal::HostAddressFamily::inet : hal::HostAddressFamily::inet6;
    int open_error{};
    std::shared_ptr<hal::HostSocket> host = core.config.host->Open(family, hal::HostSocketType::stream, &open_error);
    if (!host) return open_error != 0 ? open_error : -kEnetunreach;
    if (state->no_delay) static_cast<void>(host->SetOption(hal::HostSocketOption::no_delay, 1));
    if (state->keep_alive) static_cast<void>(host->SetOption(hal::HostSocketOption::keep_alive, 1));
    state->host = host;
    state->peer = address;
    state->has_peer = true;
    lock.unlock();
    const auto started = host->Connect(ToHost(address));
    if (core.config.logger != nullptr)
        core.config.logger->Write(core::LogLevel::info, "runtime.net", "egress connect", {},
                                  {{"address", FormatNumericAddress(address)},
                                   {"port", static_cast<std::uint64_t>(address.port)},
                                   {"result", static_cast<std::int64_t>(started)}});
    lock.lock();
    if (state->closed) return -kEbadf;
    if (started == 0) {
        state->connected = true;
        core.NotifyLocked();
        return 0;
    }
    if (started != -kEinprogress) {
        state->host.reset();
        state->has_peer = false;
        return started;
    }
    state->connecting = true;
    if (context.non_blocking) return -kEinprogress;
    const Deadline deadline(state->send_timeout_us);
    for (;;) {
        lock.unlock();
        const auto ready = host->Wait(false, true, deadline.Slice());
        lock.lock();
        if (state->closed || core.stopped.load()) return -kEbadf;
        if (ready.writable || ready.error || ready.hangup) break;
        if (context.Interrupted()) return -kEintr;
        if (deadline.Expired()) return -kEinprogress;  // SO_SNDTIMEO: Linux reports EINPROGRESS.
    }
    state->connecting = false;
    lock.unlock();
    const auto result = host->TakeError();
    if (result != 0 && core.config.logger != nullptr)
        core.config.logger->Write(core::LogLevel::warn, "runtime.net", "egress connect failed", {},
                                  {{"address", FormatNumericAddress(address)},
                                   {"port", static_cast<std::uint64_t>(address.port)},
                                   {"errno", static_cast<std::int64_t>(-result)}});
    lock.lock();
    if (result != 0) {
        state->host.reset();
        state->has_peer = false;
        return result;
    }
    state->connected = true;
    core.NotifyLocked();
    return 0;
}

int GuestNetworkStack::Listen(const int descriptor, const int backlog) {
    int error{};
    const auto socket = SocketOf(*core_, descriptor, error);
    if (!socket) return error;
    const auto& state = socket->State();
    std::scoped_lock lock(core_->mutex);
    if (state->closed) return -kEbadf;
    if (state->type != kSockStream) return -kEopnotsupp;
    if (state->connected || state->connecting) return -kEinval;
    if (state->domain == kAfUnix) {
        if (!state->bound) return -kEinval;
    } else if (const auto bound = AutoBindLocked(*core_, state); bound < 0) {
        return bound;
    }
    state->listening = true;
    state->backlog = std::clamp(backlog, 0, 128);
    return 0;
}

int GuestNetworkStack::Accept(const int descriptor, GuestSocketAddress* peer, const int flags) {
    if ((flags & ~(kSockNonBlock | kSockCloexec)) != 0) return -kEinval;
    int error{};
    const auto socket = SocketOf(*core_, descriptor, error);
    if (!socket) return error;
    const auto context = ContextOf(*core_, descriptor);
    const auto& state = socket->State();
    auto& core = *core_;
    std::unique_lock lock(core.mutex);
    if (!state->listening) return -kEinval;
    const Deadline deadline(state->receive_timeout_us);
    while (state->pending.empty()) {
        if (context.non_blocking) return -kEagain;
        if (const auto waited = WaitSlice(core, lock, *state, context, deadline, true); waited < 0) return waited;
    }
    auto accepted = std::move(state->pending.front());
    state->pending.pop_front();
    if (peer != nullptr) *peer = accepted->peer;
    lock.unlock();
    return Install(core, core_, std::move(accepted), flags);
}

int GuestNetworkStack::LocalName(const int descriptor, GuestSocketAddress& address) {
    int error{};
    const auto socket = SocketOf(*core_, descriptor, error);
    if (!socket) return error;
    const auto& state = socket->State();
    std::unique_lock lock(core_->mutex);
    if (state->closed) return -kEbadf;
    address = state->local;
    if (state->domain == kAfUnix) {
        if (!state->bound) address = GuestSocketAddress::Unix({});
        return 0;
    }
    if (const auto host = state->host) {
        lock.unlock();
        hal::HostSocketAddress local;
        if (host->LocalAddress(local) == 0) {
            // The emulated device's Wi-Fi address, never the host computer's.
            address = FromHost(local, state->domain);
            const auto device = GuestSocketAddress::Inet4(core_->config.device_ipv4, local.port);
            address = state->domain == kAfInet6 ? FromHost(ToHost(device), kAfInet6) : device;
        }
    }
    return 0;
}

int GuestNetworkStack::PeerName(const int descriptor, GuestSocketAddress& address) {
    int error{};
    const auto socket = SocketOf(*core_, descriptor, error);
    if (!socket) return error;
    const auto& state = socket->State();
    std::scoped_lock lock(core_->mutex);
    if (state->closed) return -kEbadf;
    if (!state->has_peer || (state->type == kSockStream && !state->connected)) return -kEnotconn;
    address = state->peer;
    return 0;
}

std::int64_t GuestNetworkStack::Send(const int descriptor, const std::span<const std::byte> bytes, const int flags,
                                     const GuestSocketAddress* destination) {
    int error{};
    const auto socket = SocketOf(*core_, descriptor, error);
    if (!socket) return error;
    return socket->SendBytes(bytes, flags, destination, ContextOf(*core_, descriptor));
}

std::int64_t GuestNetworkStack::Receive(const int descriptor, const std::span<std::byte> bytes, const int flags,
                                        GuestSocketAddress* source) {
    int error{};
    const auto socket = SocketOf(*core_, descriptor, error);
    if (!socket) return error;
    return socket->ReceiveBytes(bytes, flags, source, ContextOf(*core_, descriptor));
}

int GuestNetworkStack::ShutdownSocket(const int descriptor, const int how) {
    if (how < 0 || how > 2) return -kEinval;
    int error{};
    const auto socket = SocketOf(*core_, descriptor, error);
    if (!socket) return error;
    const auto& state = socket->State();
    std::unique_lock lock(core_->mutex);
    if (state->closed) return -kEbadf;
    if (state->type == kSockStream && !state->connected) return -kEnotconn;
    if (how != 1) state->shut_read = true;
    if (how != 0) {
        state->shut_write = true;
        if (state->tx) state->tx->write_closed = true;
    }
    core_->NotifyLocked();
    if (const auto host = state->host) {
        lock.unlock();
        return host->Shutdown(how);
    }
    return 0;
}

int GuestNetworkStack::SetOption(const int descriptor, const int level, const int name,
                                 const std::span<const std::byte> value) {
    int error{};
    const auto socket = SocketOf(*core_, descriptor, error);
    if (!socket) return error;
    const auto& state = socket->State();
    const auto integer = [&]() -> std::optional<std::int32_t> {
        if (value.size() < 4) return std::nullopt;
        return static_cast<std::int32_t>(Load32(value, 0));
    };
    std::unique_lock lock(core_->mutex);
    if (state->closed) return -kEbadf;
    const auto host = state->host;
    const auto forward = [&](const hal::HostSocketOption option, const std::int32_t number) {
        if (!host) return 0;
        lock.unlock();
        static_cast<void>(host->SetOption(option, number));
        lock.lock();
        return 0;
    };
    if (level == kSolSocket) {
        if (name == kSoRcvTimeo || name == kSoSndTimeo) {
            if (value.size() < 8) return -kEinval;
            const auto seconds = static_cast<std::int32_t>(Load32(value, 0));
            const auto micros = static_cast<std::int32_t>(Load32(value, 4));
            if (seconds < 0 || micros < 0 || micros >= 1'000'000) return -33;  // EDOM
            const auto total = static_cast<std::int64_t>(seconds) * 1'000'000 + micros;
            (name == kSoRcvTimeo ? state->receive_timeout_us : state->send_timeout_us) = total;
            return 0;
        }
        if (name == kSoLinger) {
            if (value.size() < 8) return -kEinval;
            state->linger_on = static_cast<std::int32_t>(Load32(value, 0));
            state->linger_seconds = static_cast<std::int32_t>(Load32(value, 4));
            return 0;
        }
        const auto number = integer();
        if (!number) return -kEinval;
        switch (name) {
        case kSoReuseAddr: state->reuse_address = *number != 0; return 0;
        case kSoKeepAlive:
            state->keep_alive = *number != 0;
            return forward(hal::HostSocketOption::keep_alive, *number != 0);
        case kSoBroadcast:
            state->broadcast = *number != 0;
            return forward(hal::HostSocketOption::broadcast, *number != 0);
        case kSoRcvBuf:
            // Linux doubles the requested size for bookkeeping overhead.
            state->receive_buffer = std::clamp(*number, 128, 0x7fffffff / 2) * 2;
            return forward(hal::HostSocketOption::receive_buffer, *number);
        case kSoSndBuf:
            state->send_buffer = std::clamp(*number, 128, 0x7fffffff / 2) * 2;
            return forward(hal::HostSocketOption::send_buffer, *number);
        case kSoDontRoute:
        case kSoOobInline:
        case kSoReusePort:
        case kSoRcvLowat:
        case kSoSndLowat: state->int_options[{level, name}] = *number; return 0;
        default: return -kEnoprotoopt;
        }
    }
    const auto number = integer();
    if (!number) return -kEinval;
    if (level == kIpprotoTcp) {
        if (state->domain == kAfUnix || state->type != kSockStream) return -kEopnotsupp;
        if (name == kTcpNoDelay) {
            state->no_delay = *number != 0;
            return forward(hal::HostSocketOption::no_delay, *number != 0);
        }
        state->int_options[{level, name}] = *number;
        return 0;
    }
    if (level == kIpprotoIp) {
        if (state->domain == kAfUnix) return -kEopnotsupp;
        if (name == kIpTtl) {
            state->int_options[{level, name}] = *number;
            return forward(hal::HostSocketOption::ttl, *number);
        }
        // IP_TOS, multicast TTL/loop/interface: stored, no egress effect.
        if (name == 1 || name == 32 || name == 33 || name == 34) {
            state->int_options[{level, name}] = *number;
            return 0;
        }
        return -kEnoprotoopt;
    }
    if (level == kIpprotoIpv6) {
        if (state->domain != kAfInet6) return -kEopnotsupp;
        if (name == kIpv6V6Only) {
            state->v6_only = *number != 0;
            return 0;
        }
        // Unicast/multicast hops, multicast loop/interface, traffic class.
        if (name == 16 || name == 17 || name == 18 || name == 19 || name == 67) {
            state->int_options[{level, name}] = *number;
            return 0;
        }
        return -kEnoprotoopt;
    }
    return -kEnoprotoopt;
}

int GuestNetworkStack::GetOption(const int descriptor, const int level, const int name, std::vector<std::byte>& value) {
    int error{};
    const auto socket = SocketOf(*core_, descriptor, error);
    if (!socket) return error;
    const auto& state = socket->State();
    std::unique_lock lock(core_->mutex);
    if (state->closed) return -kEbadf;
    if (level == kSolSocket) {
        switch (name) {
        case kSoType: value = IntValue(state->type); return 0;
        case kSoDomain: value = IntValue(state->domain); return 0;
        case kSoProtocol: value = IntValue(state->protocol); return 0;
        case kSoAcceptConn: value = IntValue(state->listening ? 1 : 0); return 0;
        case kSoReuseAddr: value = IntValue(state->reuse_address ? 1 : 0); return 0;
        case kSoKeepAlive: value = IntValue(state->keep_alive ? 1 : 0); return 0;
        case kSoBroadcast: value = IntValue(state->broadcast ? 1 : 0); return 0;
        case kSoRcvBuf: value = IntValue(state->receive_buffer); return 0;
        case kSoSndBuf: value = IntValue(state->send_buffer); return 0;
        case kSoError: {
            auto pending = std::exchange(state->pending_error, 0);
            if (pending == 0 && state->connecting && state->host) {
                const auto host = state->host;
                lock.unlock();
                const auto ready = host->Wait(false, true, std::chrono::milliseconds(0));
                if (ready.writable || ready.error || ready.hangup) {
                    const auto result = host->TakeError();
                    lock.lock();
                    state->connecting = false;
                    if (result == 0) {
                        state->connected = true;
                    } else {
                        pending = -result;
                        state->host.reset();
                    }
                    core_->NotifyLocked();
                }
            }
            value = IntValue(pending);
            return 0;
        }
        case kSoLinger:
            value = IntValue(state->linger_on);
            Store32(value, static_cast<std::uint32_t>(state->linger_seconds));
            return 0;
        case kSoRcvTimeo:
        case kSoSndTimeo: {
            const auto micros = name == kSoRcvTimeo ? state->receive_timeout_us : state->send_timeout_us;
            value = IntValue(static_cast<std::int32_t>(micros / 1'000'000));
            Store32(value, static_cast<std::uint32_t>(micros % 1'000'000));
            return 0;
        }
        case kSoPeerCred:
            if (state->domain != kAfUnix) return -kEnoprotoopt;
            value = IntValue(core_->config.pid);
            Store32(value, core_->config.uid);
            Store32(value, core_->config.gid);
            return 0;
        default:
            if (const auto found = state->int_options.find({level, name}); found != state->int_options.end()) {
                value = IntValue(found->second);
                return 0;
            }
            if (name == kSoDontRoute || name == kSoOobInline || name == kSoReusePort) {
                value = IntValue(0);
                return 0;
            }
            if (name == kSoRcvLowat || name == kSoSndLowat) {
                value = IntValue(1);
                return 0;
            }
            return -kEnoprotoopt;
        }
    }
    if (level == kIpprotoTcp && name == kTcpNoDelay) {
        if (state->type != kSockStream || state->domain == kAfUnix) return -kEopnotsupp;
        value = IntValue(state->no_delay ? 1 : 0);
        return 0;
    }
    if (level == kIpprotoIpv6 && name == kIpv6V6Only) {
        if (state->domain != kAfInet6) return -kEopnotsupp;
        value = IntValue(state->v6_only ? 1 : 0);
        return 0;
    }
    if (const auto found = state->int_options.find({level, name}); found != state->int_options.end()) {
        value = IntValue(found->second);
        return 0;
    }
    if (level == kIpprotoIp && name == kIpTtl) {
        value = IntValue(64);
        return 0;
    }
    return -kEnoprotoopt;
}

bool GuestNetworkStack::IsSocket(const int descriptor) const {
    int error{};
    return SocketOf(*core_, descriptor, error) != nullptr;
}

GuestResolveResult GuestNetworkStack::Resolve(const std::string_view name) {
    return ResolveName(*core_, name);
}

std::optional<std::string> GuestNetworkStack::ReverseLookup(const GuestSocketAddress& address) {
    if (IsLoopback(address))
        return std::string(address.family == kAfInet6 && !IsV4Mapped(address) ? "ip6-localhost" : "localhost");
    if (core_->config.mode != NetworkMode::online || core_->config.host == nullptr) return std::nullopt;
    if (address.family != kAfInet && address.family != kAfInet6) return std::nullopt;
    return core_->config.host->ReverseLookup(ToHost(address));
}

}  // namespace ogplay::runtime::net
