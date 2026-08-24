#include "ogplay/runtime/dexvm/network_runtime.h"

#include <algorithm>
#include <utility>

namespace ogplay::runtime::dexvm {

void NetworkRuntime::Configure(NetworkPolicy policy,
                               NetworkTransport* transport) noexcept {
    policy_ = std::move(policy);
    transport_ = transport;
}

const NetworkPolicy& NetworkRuntime::Policy() const noexcept { return policy_; }

void NetworkRuntime::RequireAllowed(const std::string_view host,
                                    const bool tls,
                                    const bool datagram) const {
    if (!policy_.enabled) throw NetworkRuntimeError("network policy is offline");
    if (transport_ == nullptr)
        throw NetworkRuntimeError("network transport is not injected");
    if (tls && !policy_.allow_tls)
        throw NetworkRuntimeError("TLS is denied by network policy");
    if (datagram && !policy_.allow_datagram)
        throw NetworkRuntimeError("datagram is denied by network policy");
    if (!policy_.allowed_hosts.empty() &&
        !policy_.allowed_hosts.contains(std::string(host))) {
        throw NetworkRuntimeError("host is denied by network policy: " +
                                  std::string(host));
    }
}

std::vector<std::string> NetworkRuntime::Resolve(const std::string_view host) {
    RequireAllowed(host, false, false);
    auto result = transport_->Resolve(host);
    if (result.empty()) throw NetworkRuntimeError("host has no address");
    return result;
}

void NetworkRuntime::SetAddress(const VmObjectRef owner, std::string host,
                                std::string address) {
    addresses_[owner.Value()] =
        Endpoint{std::move(host), std::move(address), 0};
}

const NetworkRuntime::Endpoint& NetworkRuntime::Address(
    const VmObjectRef owner) const {
    const auto found = addresses_.find(owner.Value());
    if (found == addresses_.end())
        throw NetworkRuntimeError("InetAddress state is unavailable");
    return found->second;
}

void NetworkRuntime::SetEndpoint(const VmObjectRef owner, Endpoint endpoint) {
    endpoints_[owner.Value()] = std::move(endpoint);
}

const NetworkRuntime::Endpoint& NetworkRuntime::GetEndpoint(
    const VmObjectRef owner) const {
    const auto found = endpoints_.find(owner.Value());
    if (found == endpoints_.end())
        throw NetworkRuntimeError("socket address state is unavailable");
    return found->second;
}

void NetworkRuntime::CreateSocket(const VmObjectRef owner, const bool tls) {
    sockets_[owner.Value()] = Socket{.tls = tls};
}

void NetworkRuntime::Connect(const VmObjectRef owner, Endpoint endpoint) {
    auto& socket = GetSocket(owner);
    if (socket.closed) throw NetworkRuntimeError("socket is closed");
    RequireAllowed(endpoint.host, socket.tls, false);
    socket.channel = transport_->Connect(endpoint.host, endpoint.port,
                                         socket.tls);
    socket.endpoint = std::move(endpoint);
    socket.connected = true;
}

NetworkRuntime::Socket& NetworkRuntime::GetSocket(const VmObjectRef owner) {
    const auto found = sockets_.find(owner.Value());
    if (found == sockets_.end())
        throw NetworkRuntimeError("socket state is unavailable");
    return found->second;
}

const NetworkRuntime::Socket& NetworkRuntime::GetSocket(
    const VmObjectRef owner) const {
    const auto found = sockets_.find(owner.Value());
    if (found == sockets_.end())
        throw NetworkRuntimeError("socket state is unavailable");
    return found->second;
}

void NetworkRuntime::BindStream(const VmObjectRef stream,
                                const VmObjectRef socket,
                                const bool output) {
    if (!GetSocket(socket).connected)
        throw NetworkRuntimeError("socket is not connected");
    streams_[stream.Value()] = Stream{socket, output};
}

std::vector<std::byte> NetworkRuntime::ReadStream(const VmObjectRef stream,
                                                  const std::size_t maximum) {
    const auto found = streams_.find(stream.Value());
    if (found == streams_.end() || found->second.output)
        throw NetworkRuntimeError("network input stream is unavailable");
    const auto& socket = GetSocket(found->second.socket);
    if (socket.closed) throw NetworkRuntimeError("socket is closed");
    return transport_->Receive(socket.channel, maximum);
}

void NetworkRuntime::WriteStream(const VmObjectRef stream,
                                 const std::span<const std::byte> bytes) {
    const auto found = streams_.find(stream.Value());
    if (found == streams_.end() || !found->second.output)
        throw NetworkRuntimeError("network output stream is unavailable");
    const auto& socket = GetSocket(found->second.socket);
    if (socket.closed) throw NetworkRuntimeError("socket is closed");
    transport_->Send(socket.channel, bytes);
}

void NetworkRuntime::CloseSocket(const VmObjectRef owner) noexcept {
    const auto found = sockets_.find(owner.Value());
    if (found == sockets_.end() || found->second.closed) return;
    if (transport_ != nullptr && found->second.connected)
        transport_->Close(found->second.channel);
    found->second.closed = true;
    found->second.connected = false;
}

void NetworkRuntime::SetPacket(const VmObjectRef owner,
                               DatagramPacket packet) {
    packets_[owner.Value()] = std::move(packet);
}

NetworkRuntime::DatagramPacket& NetworkRuntime::Packet(
    const VmObjectRef owner) {
    const auto found = packets_.find(owner.Value());
    if (found == packets_.end())
        throw NetworkRuntimeError("datagram packet state is unavailable");
    return found->second;
}

void NetworkRuntime::SendPacket(const VmObjectRef owner,
                                const std::span<const std::byte> bytes) {
    const auto& packet = Packet(owner);
    RequireAllowed(packet.endpoint.host, false, true);
    transport_->SendDatagram(
        {packet.endpoint.host, packet.endpoint.port,
         std::vector<std::byte>(bytes.begin(), bytes.end())});
}

NetworkDatagram NetworkRuntime::ReceivePacket(const std::size_t maximum) {
    if (!policy_.enabled)
        throw NetworkRuntimeError("network policy is offline");
    if (transport_ == nullptr)
        throw NetworkRuntimeError("network transport is not injected");
    if (!policy_.allow_datagram)
        throw NetworkRuntimeError("datagram is denied by network policy");
    return transport_->ReceiveDatagram(maximum);
}

void NetworkRuntime::Trace(const VmObjectRef owner,
                           const std::function<void(VmObjectRef)>& visit) const {
    if (const auto found = streams_.find(owner.Value()); found != streams_.end())
        visit(found->second.socket);
    if (const auto found = packets_.find(owner.Value()); found != packets_.end())
        visit(found->second.array);
}

void NetworkRuntime::Sweep(const VmObjectRef owner) noexcept {
    CloseSocket(owner);
    addresses_.erase(owner.Value());
    endpoints_.erase(owner.Value());
    sockets_.erase(owner.Value());
    streams_.erase(owner.Value());
    packets_.erase(owner.Value());
}

}  // namespace ogplay::runtime::dexvm
