#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ogplay/runtime/dexvm/dexvm_types.h"

namespace ogplay::runtime::dexvm {

class NetworkRuntimeError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct NetworkPolicy final {
    bool enabled{};
    bool allow_tls{};
    bool allow_datagram{};
    std::unordered_set<std::string> allowed_hosts;
};

struct NetworkDatagram final {
    std::string host;
    std::uint16_t port{};
    std::vector<std::byte> payload;
};

// Transport is injected by the process owner. Core never opens a host socket.
class NetworkTransport {
public:
    virtual ~NetworkTransport() = default;
    [[nodiscard]] virtual std::vector<std::string>
    Resolve(std::string_view host) = 0;
    [[nodiscard]] virtual std::uint64_t Connect(
        std::string_view host, std::uint16_t port, bool tls) = 0;
    virtual void Send(std::uint64_t channel,
                      std::span<const std::byte> bytes) = 0;
    [[nodiscard]] virtual std::vector<std::byte>
    Receive(std::uint64_t channel, std::size_t maximum) = 0;
    virtual void Close(std::uint64_t channel) noexcept = 0;
    virtual void SendDatagram(const NetworkDatagram& datagram) = 0;
    [[nodiscard]] virtual NetworkDatagram ReceiveDatagram(
        std::size_t maximum) = 0;
};

class NetworkRuntime final {
public:
    NetworkRuntime() = default;
    ~NetworkRuntime();
    NetworkRuntime(const NetworkRuntime&) = delete;
    NetworkRuntime& operator=(const NetworkRuntime&) = delete;

    struct Endpoint final {
        std::string host;
        std::string address;
        std::uint16_t port{};
    };
    struct Socket final {
        Endpoint endpoint;
        std::uint64_t channel{};
        bool tls{};
        bool connected{};
        bool closed{};
    };
    struct DatagramPacket final {
        VmObjectRef array;
        std::int32_t offset{};
        std::int32_t length{};
        Endpoint endpoint;
    };

    void Configure(NetworkPolicy policy, NetworkTransport* transport) noexcept;
    [[nodiscard]] const NetworkPolicy& Policy() const noexcept;
    [[nodiscard]] std::vector<std::string> Resolve(std::string_view host);

    void SetAddress(VmObjectRef owner, std::string host, std::string address);
    [[nodiscard]] const Endpoint& Address(VmObjectRef owner) const;
    void SetEndpoint(VmObjectRef owner, Endpoint endpoint);
    [[nodiscard]] const Endpoint& GetEndpoint(VmObjectRef owner) const;

    void CreateSocket(VmObjectRef owner, bool tls = false);
    void Connect(VmObjectRef owner, Endpoint endpoint);
    [[nodiscard]] Socket& GetSocket(VmObjectRef owner);
    [[nodiscard]] const Socket& GetSocket(VmObjectRef owner) const;
    void BindStream(VmObjectRef stream, VmObjectRef socket, bool output);
    [[nodiscard]] std::vector<std::byte> ReadStream(VmObjectRef stream,
                                                    std::size_t maximum);
    void WriteStream(VmObjectRef stream, std::span<const std::byte> bytes);
    void CloseSocket(VmObjectRef owner) noexcept;
    void Shutdown() noexcept;

    void SetPacket(VmObjectRef owner, DatagramPacket packet);
    [[nodiscard]] DatagramPacket& Packet(VmObjectRef owner);
    void SendPacket(VmObjectRef owner, std::span<const std::byte> bytes);
    [[nodiscard]] NetworkDatagram ReceivePacket(std::size_t maximum);

    void Trace(VmObjectRef owner,
               const std::function<void(VmObjectRef)>& visit) const;
    void Sweep(VmObjectRef owner) noexcept;

private:
    void RequireAllowed(std::string_view host, bool tls, bool datagram) const;
    NetworkPolicy policy_;
    NetworkTransport* transport_{};
    std::unordered_map<std::uint32_t, Endpoint> addresses_;
    std::unordered_map<std::uint32_t, Endpoint> endpoints_;
    std::unordered_map<std::uint32_t, Socket> sockets_;
    struct Stream final { VmObjectRef socket; bool output{}; };
    std::unordered_map<std::uint32_t, Stream> streams_;
    std::unordered_map<std::uint32_t, DatagramPacket> packets_;
};

}  // namespace ogplay::runtime::dexvm
