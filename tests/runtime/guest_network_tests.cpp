// Guest socket stack (runtime/net): in-process AF_UNIX and loopback, offline
// failures, the dnsproxyd protocol, the host egress and the ARM syscall ABI.

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "host_network_fixture.h"
#include "ogplay/core/capability_ledger.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/net/guest_network.h"
#include "ogplay/runtime/syscall/syscall.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace {

using namespace std::chrono_literals;
namespace net = ogplay::runtime::net;
namespace runtime = ogplay::runtime;
using ogplay::memory::GuestAddress;

constexpr int kEagain = 11;
constexpr int kEpipe = 32;
constexpr int kEnoent = 2;
constexpr int kEnetunreach = 101;
constexpr int kEconnrefused = 111;
constexpr int kEinprogress = 115;

struct NetFixture final {
    runtime::VirtualFileSystem vfs;
    net::GuestNetworkStack stack;

    explicit NetFixture(const net::NetworkMode mode = net::NetworkMode::disabled,
                        ogplay::hal::HostNetwork* host = nullptr)
        : stack(vfs, {.mode = mode, .host = host, .pid = 1234, .uid = 10001, .gid = 10001}) {}

    std::size_t Write(const int fd, const std::string_view text) {
        return vfs.Write(fd, std::as_bytes(std::span{text.data(), text.size()}));
    }

    std::string Read(const int fd, const std::size_t maximum = 256) {
        std::vector<std::byte> buffer(maximum);
        const auto count = vfs.Read(fd, buffer);
        return {reinterpret_cast<const char*>(buffer.data()), count};
    }

    std::string Receive(const int fd, net::GuestSocketAddress* from = nullptr, const int flags = 0) {
        std::vector<std::byte> buffer(256);
        const auto count = stack.Receive(fd, buffer, flags, from);
        REQUIRE(count >= 0);
        return {reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(count)};
    }

    std::int64_t Send(const int fd, const std::string_view text, const net::GuestSocketAddress* to = nullptr) {
        return stack.Send(fd, std::as_bytes(std::span{text.data(), text.size()}), 0, to);
    }
};

net::GuestSocketAddress Loopback(const std::uint16_t port) {
    return net::GuestSocketAddress::Inet4({127, 0, 0, 1}, port);
}

// Reads a whole dnsproxyd reply: the client closes after one answer.
std::vector<std::byte> ProxyQuery(NetFixture& f, const std::string& command) {
    const auto fd = f.stack.Socket(net::kAfUnix, net::kSockStream, 0);
    REQUIRE(fd >= 0);
    REQUIRE(f.stack.Connect(fd, net::GuestSocketAddress::Unix("/dev/socket/dnsproxyd")) == 0);
    auto request = command;
    request.push_back('\0');
    REQUIRE(f.Write(fd, request) == request.size());
    std::vector<std::byte> reply;
    for (;;) {
        std::vector<std::byte> chunk(512);
        const auto count = f.vfs.Read(fd, chunk);
        if (count == 0) break;
        reply.insert(reply.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(count));
    }
    f.vfs.Close(fd);
    return reply;
}

std::uint32_t BigEndian(const std::vector<std::byte>& bytes, const std::size_t offset) {
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index)
        value = (value << 8U) | std::to_integer<std::uint32_t>(bytes[offset + index]);
    return value;
}

}  // namespace

TEST_CASE("NET-01 socketpair streams share VFS descriptors, shutdown and close semantics") {
    NetFixture f;
    std::array<int, 2> pair{};
    REQUIRE(f.stack.SocketPair(net::kAfUnix, net::kSockStream, 0, pair) == 0);
    CHECK(f.vfs.DescriptorInfo(pair[0]).is_socket);
    CHECK((f.vfs.DescriptorInfo(pair[0]).Mode() & 0170000U) == 0140000U);
    CHECK(f.Write(pair[0], "ping") == 4);
    CHECK(f.vfs.PipeBuffered(pair[1]) == std::optional<std::size_t>{4});
    CHECK((*f.vfs.PipePollEvents(pair[1]) & 1U) != 0);
    CHECK(f.Read(pair[1]) == "ping");

    // A dup shares the socket; closing one number keeps it open.
    const auto copy = f.vfs.Duplicate(pair[1]);
    f.vfs.Close(pair[1]);
    CHECK(f.Write(pair[0], "x") == 1);
    CHECK(f.Read(copy) == "x");

    // O_NONBLOCK lives on the open description and is changed by F_SETFL.
    f.vfs.SetNonBlocking(copy, true);
    CHECK_THROWS_WITH_AS(static_cast<void>(f.Read(copy)), "socket read failed", runtime::VfsError);
    try {
        static_cast<void>(f.Read(copy));
    } catch (const runtime::VfsError& error) {
        CHECK(error.ErrorNumber() == kEagain);
    }
    f.vfs.SetNonBlocking(copy, false);

    // Blocked readers wake when the peer writes.
    auto blocked = std::async(std::launch::async, [&] { return f.Read(copy); });
    CHECK(blocked.wait_for(30ms) == std::future_status::timeout);
    CHECK(f.Write(pair[0], "late") == 4);
    REQUIRE(blocked.wait_for(2s) == std::future_status::ready);
    CHECK(blocked.get() == "late");

    CHECK(f.stack.ShutdownSocket(pair[0], 1) == 0);
    CHECK(f.Read(copy).empty());  // EOF after the peer shut its write side.
    f.vfs.Close(copy);
    CHECK(f.Send(pair[0], "gone") == -kEpipe);
    CHECK(f.stack.IsSocket(pair[0]));
    f.vfs.Close(pair[0]);
    CHECK_FALSE(f.stack.IsSocket(pair[0]));
}

TEST_CASE("NET-02 loopback TCP connects in process and reports both names") {
    NetFixture f;
    const auto listener = f.stack.Socket(net::kAfInet, net::kSockStream | net::kSockCloexec, 0);
    REQUIRE(listener >= 0);
    CHECK(f.vfs.CloseOnExec(listener));
    REQUIRE(f.stack.Bind(listener, Loopback(0)) == 0);
    REQUIRE(f.stack.Listen(listener, 4) == 0);
    net::GuestSocketAddress bound;
    REQUIRE(f.stack.LocalName(listener, bound) == 0);
    CHECK(bound.port >= 32768);
    CHECK(net::FormatNumericAddress(bound) == "127.0.0.1");

    // A second bind on the same port fails.
    const auto rival = f.stack.Socket(net::kAfInet, net::kSockStream, 0);
    CHECK(f.stack.Bind(rival, Loopback(bound.port)) == -98);

    // IPv6 sockets reach the IPv4 listener through IPv4-mapped addresses, as libcore uses them.
    const auto client = f.stack.Socket(net::kAfInet6, net::kSockStream, 0);
    auto mapped = *net::ParseNumericAddress("::ffff:127.0.0.1");
    mapped.port = bound.port;
    REQUIRE(f.stack.Connect(client, mapped) == 0);
    CHECK((*f.vfs.PipePollEvents(listener) & 1U) != 0);
    net::GuestSocketAddress peer;
    const auto server = f.stack.Accept(listener, &peer, net::kSockNonBlock);
    REQUIRE(server >= 0);
    CHECK(f.vfs.DescriptorStatus(server).non_blocking);
    net::GuestSocketAddress client_name;
    REQUIRE(f.stack.LocalName(client, client_name) == 0);
    CHECK(peer.port == client_name.port);
    CHECK(f.Send(client, "hello") == 5);
    CHECK(f.Receive(server) == "hello");
    CHECK(f.Send(server, "world") == 5);
    CHECK(f.Receive(client, nullptr, net::kMsgPeek) == "world");
    CHECK(f.Receive(client) == "world");
    net::GuestSocketAddress remote;
    REQUIRE(f.stack.PeerName(client, remote) == 0);
    CHECK(remote.port == bound.port);

    // Closing the server end gives the client EOF.
    f.vfs.Close(server);
    CHECK(f.Receive(client).empty());

    // Closed listener: connection refused.
    f.vfs.Close(listener);
    const auto late = f.stack.Socket(net::kAfInet, net::kSockStream, 0);
    CHECK(f.stack.Connect(late, Loopback(bound.port)) == -kEconnrefused);
    CHECK(f.stack.Connect(late, net::GuestSocketAddress::Unix("/missing")) == -97);
    const auto unix_client = f.stack.Socket(net::kAfUnix, net::kSockStream, 0);
    CHECK(f.stack.Connect(unix_client, net::GuestSocketAddress::Unix("/data/missing")) == -kEnoent);
    std::vector<std::byte> peer_cred;
    REQUIRE(f.stack.GetOption(unix_client, net::kSolSocket, 17, peer_cred) == 0);
    REQUIRE(peer_cred.size() == 12);
    CHECK(std::to_integer<int>(peer_cred[0]) == (1234 & 0xff));
}

TEST_CASE("NET-03 loopback UDP delivers in process and offline destinations are unreachable") {
    NetFixture f;
    const auto receiver = f.stack.Socket(net::kAfInet, net::kSockDgram, 0);
    REQUIRE(f.stack.Bind(receiver, Loopback(9000)) == 0);
    const auto sender = f.stack.Socket(net::kAfInet6, net::kSockDgram, 0);
    const auto target = Loopback(9000);
    CHECK(f.Send(sender, "datagram", &target) == 8);
    net::GuestSocketAddress from;
    CHECK(f.Receive(receiver, &from) == "datagram");
    CHECK(net::FormatNumericAddress(from) == "127.0.0.1");
    CHECK(from.port >= 32768);
    // Truncation and MSG_TRUNC report the datagram's own length.
    CHECK(f.Send(sender, "0123456789", &target) == 10);
    std::array<std::byte, 4> small{};
    CHECK(f.stack.Receive(receiver, small, net::kMsgTrunc, nullptr) == 10);

    // Receive timeouts come from SO_RCVTIMEO.
    const std::array timeout{std::byte{0},    std::byte{0},    std::byte{0}, std::byte{0},
                             std::byte{0x20}, std::byte{0x4e}, std::byte{0}, std::byte{0}};  // 20 ms
    REQUIRE(f.stack.SetOption(receiver, net::kSolSocket, 20, timeout) == 0);
    std::array<std::byte, 16> buffer{};
    const auto start = std::chrono::steady_clock::now();
    CHECK(f.stack.Receive(receiver, buffer, 0, nullptr) == -kEagain);
    CHECK(std::chrono::steady_clock::now() - start >= 15ms);
    CHECK(f.stack.Receive(receiver, buffer, net::kMsgDontWait, nullptr) == -kEagain);

    // An offline device has no route beyond loopback.
    const auto remote = net::GuestSocketAddress::Inet4({93, 184, 216, 34}, 80);
    CHECK(f.Send(sender, "x", &remote) == -kEnetunreach);
    const auto tcp = f.stack.Socket(net::kAfInet, net::kSockStream, 0);
    CHECK(f.stack.Connect(tcp, remote) == -kEnetunreach);
    CHECK_FALSE(f.stack.Resolve("example.com").found);
    CHECK(f.stack.Resolve("localhost").found);
    CHECK(f.stack.Resolve("10.1.2.3").found);
}

TEST_CASE("NET-04 dnsproxyd answers the bionic getaddrinfo and hostent protocol") {
    NetFixture f;
    // getaddrinfo <host> <service> <flags> <family> <socktype> <protocol> <iface>
    auto reply = ProxyQuery(f, "getaddrinfo localhost 80 0 2 1 0 ^");
    REQUIRE(reply.size() == 4 + 4 + 32 + 4 + 16 + 4 + 4);  // One entry, then the terminator.
    CHECK(std::string(reinterpret_cast<const char*>(reply.data()), 3) == "222");
    CHECK(BigEndian(reply, 4) == 32);  // 32-bit ARM struct addrinfo.
    CHECK(std::to_integer<int>(reply[8 + 4]) == net::kAfInet);
    CHECK(std::to_integer<int>(reply[8 + 8]) == net::kSockStream);
    CHECK(BigEndian(reply, 40) == 16);  // sockaddr_in
    CHECK(std::to_integer<int>(reply[44 + 2]) == 0);
    CHECK(std::to_integer<int>(reply[44 + 3]) == 80);
    CHECK(std::to_integer<int>(reply[44 + 4]) == 127);
    CHECK(BigEndian(reply, 60) == 0);  // No canonical name.
    CHECK(BigEndian(reply, reply.size() - 4) == 0);

    // Offline names fail like netd without a network.
    reply = ProxyQuery(f, "getaddrinfo example.com ^ 1024 0 1 0 ^");
    REQUIRE(reply.size() == 8);
    CHECK(std::string(reinterpret_cast<const char*>(reply.data()), 3) == "401");

    // gethostbyname <iface> <name> <af>
    reply = ProxyQuery(f, "gethostbyname ^ localhost 2");
    REQUIRE(reply.size() > 4);
    CHECK(std::string(reinterpret_cast<const char*>(reply.data()), 3) == "222");
    CHECK(BigEndian(reply, 4) == 10);  // "localhost\0"
}

TEST_CASE("NET-05 online sockets use the host egress with the device address") {
    ogplay::test::host_network::HostTcpServer server([](const std::intptr_t socket) {
        const auto request = ogplay::test::host_network::HostTcpServer::ReceiveSome(socket);
        ogplay::test::host_network::HostTcpServer::SendAll(socket, "echo:" + request);
    });
    ogplay::test::host_network::RedirectHostNetwork host({"game.test"});
    NetFixture f(net::NetworkMode::online, &host);
    const auto resolved = f.stack.Resolve("game.test");
    REQUIRE(resolved.found);
    REQUIRE(resolved.addresses.size() == 1);
    auto address = resolved.addresses.front();
    address.port = server.Port();

    const auto socket = f.stack.Socket(net::kAfInet, net::kSockStream | net::kSockNonBlock, 0);
    const auto started = f.stack.Connect(socket, address);
    REQUIRE((started == 0 || started == -kEinprogress));
    // Non-blocking connect completes through poll and SO_ERROR.
    for (int attempt = 0; attempt < 200 && (*f.vfs.PipePollEvents(socket) & 2U) == 0; ++attempt)
        std::this_thread::sleep_for(5ms);
    std::vector<std::byte> error;
    REQUIRE(f.stack.GetOption(socket, net::kSolSocket, 4, error) == 0);
    CHECK(std::to_integer<int>(error[0]) == 0);
    f.vfs.SetNonBlocking(socket, false);
    CHECK(f.Send(socket, "ping") == 4);
    CHECK(f.Receive(socket) == "echo:ping");
    net::GuestSocketAddress local;
    REQUIRE(f.stack.LocalName(socket, local) == 0);
    CHECK(net::FormatNumericAddress(local) == "192.168.1.100");
    net::GuestSocketAddress peer;
    REQUIRE(f.stack.PeerName(socket, peer) == 0);
    CHECK(net::FormatNumericAddress(peer) == "203.0.113.10");
    f.vfs.Close(socket);
    CHECK(server.Accepted() == 1);

    // The DNS proxy resolves through the same host resolver.
    const auto reply = ProxyQuery(f, "gethostbyname ^ game.test 2");
    REQUIRE(reply.size() > 4);
    CHECK(std::string(reinterpret_cast<const char*>(reply.data()), 3) == "222");
    CHECK(host.Resolutions() >= 2);
}

TEST_CASE("NET-06 ARM socket syscalls, select, epoll, fcntl and ioctl share one descriptor table") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher = runtime::CreateAndroidArmSyscallDispatcher(ledger);
    runtime::VirtualFileSystem vfs;
    ogplay::memory::AddressSpace memory;
    memory.Map({GuestAddress{0x10000}, memory.PageSize()},
               ogplay::memory::PageProtection::read | ogplay::memory::PageProtection::write);
    net::GuestNetworkStack stack(vfs, {});
    runtime::BindAndroidFileSyscalls(dispatcher, vfs, memory);
    runtime::BindAndroidSocketSyscalls(dispatcher, stack, memory);
    const auto call = [&](const std::uint32_t number, const std::array<std::uint32_t, 6> arguments) {
        runtime::A32SyscallFrame frame;
        frame.number = number;
        frame.thread_id = 1;
        for (std::size_t index = 0; index < arguments.size(); ++index)
            frame.arguments[index] = arguments[index];
        return dispatcher.Dispatch(frame);
    };
    const auto write_bytes = [&](const std::uint32_t address, const std::vector<std::byte>& bytes) {
        memory.Write(GuestAddress{address}, bytes);
    };
    // socketpair(AF_UNIX, SOCK_STREAM, 0, sv)
    REQUIRE(call(288, {1, 1, 0, 0x10000, 0, 0}) == 0);
    const auto left = static_cast<std::int32_t>(memory.Read32(GuestAddress{0x10000}));
    const auto right = static_cast<std::int32_t>(memory.Read32(GuestAddress{0x10004}));
    // send(left, "abc", 3, 0); FIONREAD(right) == 3; read(right).
    write_bytes(0x10100, {std::byte{'a'}, std::byte{'b'}, std::byte{'c'}});
    CHECK(call(289, {static_cast<std::uint32_t>(left), 0x10100, 3, 0, 0, 0}) == 3);
    CHECK(call(54, {static_cast<std::uint32_t>(right), 0x541b, 0x10200, 0, 0, 0}) == 0);
    CHECK(memory.Read32(GuestAddress{0x10200}) == 3);

    // select(right + 1, {right}, NULL, NULL, {0, 0}) reports it readable.
    memory.Write32(GuestAddress{0x10300}, 1U << static_cast<unsigned>(right));
    memory.Write32(GuestAddress{0x10400}, 0);
    memory.Write32(GuestAddress{0x10404}, 0);
    CHECK(call(142, {static_cast<std::uint32_t>(right + 1), 0x10300, 0, 0, 0x10400, 0}) == 1);
    CHECK(memory.Read32(GuestAddress{0x10300}) == 1U << static_cast<unsigned>(right));

    // epoll_create1, epoll_ctl(ADD right EPOLLIN, data 77), epoll_wait.
    const auto epoll = call(357, {0, 0, 0, 0, 0, 0});
    REQUIRE(epoll >= 0);
    memory.Write32(GuestAddress{0x10500}, 1);
    memory.Write32(GuestAddress{0x10508}, 77);
    memory.Write32(GuestAddress{0x1050c}, 0);
    REQUIRE(call(251, {static_cast<std::uint32_t>(epoll), 1, static_cast<std::uint32_t>(right), 0x10500, 0, 0}) == 0);
    CHECK(call(252, {static_cast<std::uint32_t>(epoll), 0x10600, 4, 0, 0, 0}) == 1);
    CHECK(memory.Read32(GuestAddress{0x10600}) == 1);
    CHECK(memory.Read32(GuestAddress{0x10608}) == 77);

    CHECK(call(3, {static_cast<std::uint32_t>(right), 0x10700, 16, 0, 0, 0}) == 3);
    CHECK(call(252, {static_cast<std::uint32_t>(epoll), 0x10600, 4, 0, 0, 0}) == 0);

    // fcntl(F_SETFL, O_NONBLOCK) then recv reports EAGAIN.
    CHECK(call(55, {static_cast<std::uint32_t>(right), 4, 0x802, 0, 0, 0}) == 0);
    CHECK((call(55, {static_cast<std::uint32_t>(right), 3, 0, 0, 0, 0}) & 0x800) != 0);
    CHECK(call(291, {static_cast<std::uint32_t>(right), 0x10700, 16, 0, 0, 0}) == -kEagain);

    // An AF_INET socket bound through sockaddr_in and named by getsockname.
    const auto udp = call(281, {2, 2, 0, 0, 0, 0});
    REQUIRE(udp >= 0);
    const auto sockaddr = net::EncodeSockaddr(Loopback(9100));
    write_bytes(0x10800, sockaddr);
    CHECK(call(282, {static_cast<std::uint32_t>(udp), 0x10800, static_cast<std::uint32_t>(sockaddr.size()), 0, 0, 0}) ==
          0);
    memory.Write32(GuestAddress{0x10900}, 16);
    CHECK(call(286, {static_cast<std::uint32_t>(udp), 0x10880, 0x10900, 0, 0, 0}) == 0);
    CHECK(memory.Read32(GuestAddress{0x10900}) == 16);
    std::array<std::byte, 4> port{};
    memory.Read(GuestAddress{0x10880}, port);
    CHECK(std::to_integer<int>(port[2]) * 256 + std::to_integer<int>(port[3]) == 9100);
    CHECK(call(6, {static_cast<std::uint32_t>(udp), 0, 0, 0, 0, 0}) == 0);
    CHECK_FALSE(stack.IsSocket(udp));
    CHECK(ledger.Unimplemented().empty());
}
