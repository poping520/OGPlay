#include "host_network_fixture.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ogplay::test::host_network {
namespace {

#if defined(_WIN32)
using Native = SOCKET;

void CloseNative(const std::intptr_t socket) {
    closesocket(static_cast<SOCKET>(socket));
}

struct WinsockInit final {
    WinsockInit() {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
    }

    ~WinsockInit() { WSACleanup(); }
};

WinsockInit& Winsock() {
    static WinsockInit init;
    return init;
}
#else
using Native = int;

void CloseNative(const std::intptr_t socket) {
    ::close(static_cast<int>(socket));
}
#endif

class RedirectSocket final : public hal::HostSocket {
public:
    explicit RedirectSocket(std::unique_ptr<hal::HostSocket> inner) : inner_(std::move(inner)) {}

    int Connect(const hal::HostSocketAddress& address) override { return inner_->Connect(Redirect(address)); }

    int TakeError() override { return inner_->TakeError(); }

    int Bind(const hal::HostSocketAddress& address) override { return inner_->Bind(address); }

    std::int64_t Send(const std::span<const std::byte> bytes) override { return inner_->Send(bytes); }

    std::int64_t SendTo(const std::span<const std::byte> bytes, const hal::HostSocketAddress& to) override {
        return inner_->SendTo(bytes, Redirect(to));
    }

    std::int64_t Receive(const std::span<std::byte> bytes, const bool peek) override {
        return inner_->Receive(bytes, peek);
    }

    std::int64_t ReceiveFrom(const std::span<std::byte> bytes, const bool peek, hal::HostSocketAddress& from) override {
        return inner_->ReceiveFrom(bytes, peek, from);
    }

    int Shutdown(const int how) override { return inner_->Shutdown(how); }

    int LocalAddress(hal::HostSocketAddress& address) override { return inner_->LocalAddress(address); }

    int PeerAddress(hal::HostSocketAddress& address) override { return inner_->PeerAddress(address); }

    int SetOption(const hal::HostSocketOption option, const int value) override {
        return inner_->SetOption(option, value);
    }

    std::optional<std::size_t> Available() override { return inner_->Available(); }

    hal::HostReadiness Wait(const bool read, const bool write, const std::chrono::milliseconds timeout) override {
        return inner_->Wait(read, write, timeout);
    }

private:
    static hal::HostSocketAddress Redirect(hal::HostSocketAddress address) {
        if (address.family == hal::HostAddressFamily::inet &&
            std::equal(kRedirectAddress.begin(), kRedirectAddress.end(), address.address.begin()))
            address.address = {127, 0, 0, 1};
        return address;
    }

    std::unique_ptr<hal::HostSocket> inner_;
};

}  // namespace

HostTcpServer::HostTcpServer(Handler handler) : handler_(std::move(handler)) {
#if defined(_WIN32)
    static_cast<void>(Winsock());
#endif
    const auto listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == static_cast<Native>(-1)) throw std::runtime_error("host test server socket failed");
    listener_ = static_cast<std::intptr_t>(listener);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(listener, 8) != 0)
        throw std::runtime_error("host test server bind failed");
#if defined(_WIN32)
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this, listener] {
        while (!stopping_.load()) {
#if defined(_WIN32)
            WSAPOLLFD entry{listener, POLLRDNORM, 0};
            if (WSAPoll(&entry, 1, 20) <= 0) continue;
#else
            pollfd entry{listener, POLLIN, 0};
            if (::poll(&entry, 1, 20) <= 0) continue;
#endif
            const auto client = ::accept(listener, nullptr, nullptr);
            if (client == static_cast<Native>(-1)) continue;
            ++accepted_;
            workers_.emplace_back([this, client] {
                handler_(static_cast<std::intptr_t>(client));
                CloseNative(static_cast<std::intptr_t>(client));
            });
        }
    });
}

HostTcpServer::~HostTcpServer() {
    stopping_ = true;
    thread_.join();
    for (auto& worker : workers_)
        worker.join();
    CloseNative(listener_);
}

std::string HostTcpServer::ReceiveSome(const std::intptr_t socket) {
    char buffer[4096];
    const auto count = ::recv(static_cast<Native>(socket), buffer, sizeof(buffer), 0);
    return count > 0 ? std::string(buffer, static_cast<std::size_t>(count)) : std::string();
}

void HostTcpServer::SendAll(const std::intptr_t socket, std::string_view bytes) {
    while (!bytes.empty()) {
        const auto sent = ::send(static_cast<Native>(socket), bytes.data(), static_cast<int>(bytes.size()), 0);
        if (sent <= 0) return;
        bytes.remove_prefix(static_cast<std::size_t>(sent));
    }
}

RedirectHostNetwork::RedirectHostNetwork(std::vector<std::string> names)
    : system_(hal::CreateSystemHostNetwork()), names_(std::move(names)) {}

std::unique_ptr<hal::HostSocket> RedirectHostNetwork::Open(const hal::HostAddressFamily family,
                                                           const hal::HostSocketType type, int* error) {
    auto inner = system_->Open(family, type, error);
    if (!inner) return nullptr;
    return std::make_unique<RedirectSocket>(std::move(inner));
}

hal::HostResolveResult RedirectHostNetwork::Resolve(const std::string_view name) {
    ++resolutions_;
    hal::HostResolveResult result;
    if (std::find(names_.begin(), names_.end(), name) == names_.end()) return result;
    hal::HostSocketAddress address;
    std::copy(kRedirectAddress.begin(), kRedirectAddress.end(), address.address.begin());
    result.found = true;
    result.addresses.push_back(address);
    result.canonical_name = std::string(name);
    return result;
}

std::optional<std::string> RedirectHostNetwork::ReverseLookup(const hal::HostSocketAddress&) {
    return std::nullopt;
}

}  // namespace ogplay::test::host_network
