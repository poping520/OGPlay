// ARM EABI socket syscalls over the guest socket stack (runtime/net).

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "ogplay/runtime/net/guest_network.h"
#include "ogplay/runtime/syscall/syscall.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {
namespace {

constexpr std::int32_t kEfault = 14;
constexpr std::int32_t kEinval = 22;
constexpr std::int32_t kEnotty = 25;
constexpr std::int32_t kEmsgsize = 90;
constexpr std::int32_t kEopnotsupp = 95;
// Bounds the host scratch of one transfer; sockets may return short counts.
constexpr std::size_t kMaxTransfer = 256U * 1024U;
constexpr std::uint32_t kMaxSockaddr = 128;
constexpr std::uint32_t kMaxOption = 256;
constexpr std::uint32_t kMaxIovecs = 1024;  // Linux UIO_MAXIOV.

struct Memory final {
    memory::AddressSpace& space;
    std::uint64_t thread;

    std::optional<std::uint32_t> Word(const std::uint32_t address) const {
        std::array<std::byte, 4> bytes{};
        try {
            space.Read(memory::GuestAddress{address}, bytes, thread);
        } catch (const memory::MemoryFault&) {
            return std::nullopt;
        }
        std::uint32_t value{};
        for (std::size_t index = 0; index < 4; ++index)
            value |= std::to_integer<std::uint32_t>(bytes[index]) << (index * 8U);
        return value;
    }

    bool StoreWord(const std::uint32_t address, const std::uint32_t value) const {
        std::array<std::byte, 4> bytes{};
        for (std::size_t index = 0; index < 4; ++index)
            bytes[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
        try {
            space.Write(memory::GuestAddress{address}, bytes, thread);
        } catch (const memory::MemoryFault&) {
            return false;
        }
        return true;
    }

    bool ReadBytes(const std::uint32_t address, const std::span<std::byte> bytes) const {
        if (bytes.empty()) return true;
        try {
            space.Read(memory::GuestAddress{address}, bytes, thread);
        } catch (const memory::MemoryFault&) {
            return false;
        }
        return true;
    }

    bool WriteBytes(const std::uint32_t address, const std::span<const std::byte> bytes) const {
        if (bytes.empty()) return true;
        try {
            space.Write(memory::GuestAddress{address}, bytes, thread);
        } catch (const memory::MemoryFault&) {
            return false;
        }
        return true;
    }
};

// Reads a sockaddr argument; returns 0 or -errno.
std::int32_t ReadAddress(const Memory& memory, const std::uint32_t address, const std::uint32_t length,
                         net::GuestSocketAddress& out) {
    if (length > kMaxSockaddr) return -kEinval;
    std::vector<std::byte> bytes(length);
    if (!memory.ReadBytes(address, bytes)) return -kEfault;
    return net::DecodeSockaddr(bytes, out);
}

// Linux move_addr_to_user: copy at most *length bytes, then store the full size.
std::int32_t WriteAddress(const Memory& memory, const std::uint32_t address, const std::uint32_t length_address,
                          const net::GuestSocketAddress& value) {
    if (address == 0 || length_address == 0) return 0;
    const auto capacity = memory.Word(length_address);
    if (!capacity) return -kEfault;
    if (std::bit_cast<std::int32_t>(*capacity) < 0) return -kEinval;
    const auto encoded = net::EncodeSockaddr(value);
    const auto count = std::min<std::size_t>(*capacity, encoded.size());
    if (!memory.WriteBytes(address, std::span(encoded).first(count))) return -kEfault;
    if (!memory.StoreWord(length_address, static_cast<std::uint32_t>(encoded.size()))) return -kEfault;
    return 0;
}

struct Iovec final {
    std::uint32_t base{};
    std::uint32_t length{};
};

std::optional<std::vector<Iovec>> ReadIovecs(const Memory& memory, const std::uint32_t address,
                                             const std::uint32_t count) {
    if (count > kMaxIovecs) return std::nullopt;
    std::vector<Iovec> vectors(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto base = memory.Word(address + index * 8U);
        const auto length = memory.Word(address + index * 8U + 4U);
        if (!base || !length) return std::nullopt;
        vectors[index] = {*base, *length};
    }
    return vectors;
}

A32SyscallOutcome Done(const std::int64_t value) {
    return {static_cast<std::int32_t>(value), SupervisorCallProgress::handled_idle};
}

// Data transfers advance the caller only when bytes moved.
A32SyscallOutcome Moved(const std::int64_t value) {
    return {static_cast<std::int32_t>(value),
            value > 0 ? SupervisorCallProgress::handled_advanced : SupervisorCallProgress::handled_idle};
}

}  // namespace

void BindAndroidSocketSyscalls(A32SyscallDispatcher& dispatcher, net::GuestNetworkStack& network,
                               memory::AddressSpace& address_space) {
    const auto memory_of = [&address_space](const A32SyscallFrame& frame) {
        return Memory{address_space, frame.thread_id};
    };
    const auto descriptor = [](const A32SyscallFrame& frame, const std::size_t index) {
        return std::bit_cast<std::int32_t>(frame.arguments[index]);
    };
    const auto bind_handler = [&dispatcher](const std::uint32_t number, const char* name,
                                            A32SyscallDispatcher::Handler handler) {
        try {
            dispatcher.Implement(number, handler);
        } catch (const SyscallError&) {
            dispatcher.Register(number, name, SyscallGroup::network, std::move(handler));
        }
    };

    bind_handler(281, "socket", [&network](const A32SyscallFrame& frame) -> A32SyscallOutcome {
        return Done(network.Socket(std::bit_cast<std::int32_t>(frame.arguments[0]),
                                   std::bit_cast<std::int32_t>(frame.arguments[1]),
                                   std::bit_cast<std::int32_t>(frame.arguments[2])));
    });
    bind_handler(288, "socketpair", [&network, memory_of](const A32SyscallFrame& frame) -> A32SyscallOutcome {
        std::array<int, 2> pair{};
        const auto result = network.SocketPair(std::bit_cast<std::int32_t>(frame.arguments[0]),
                                               std::bit_cast<std::int32_t>(frame.arguments[1]),
                                               std::bit_cast<std::int32_t>(frame.arguments[2]), pair);
        if (result < 0) return Done(result);
        const auto memory = memory_of(frame);
        if (!memory.StoreWord(frame.arguments[3], static_cast<std::uint32_t>(pair[0])) ||
            !memory.StoreWord(frame.arguments[3] + 4U, static_cast<std::uint32_t>(pair[1]))) {
            return Done(-kEfault);
        }
        return Done(0);
    });
    const auto address_call = [&network, memory_of, descriptor](const bool connect) {
        return [&network, memory_of, descriptor, connect](const A32SyscallFrame& frame) -> A32SyscallOutcome {
            net::GuestSocketAddress address;
            if (const auto read = ReadAddress(memory_of(frame), frame.arguments[1], frame.arguments[2], address);
                read < 0)
                return Done(read);
            const auto fd = descriptor(frame, 0);
            return Done(connect ? network.Connect(fd, address) : network.Bind(fd, address));
        };
    };
    bind_handler(282, "bind", address_call(false));
    bind_handler(283, "connect", address_call(true));
    bind_handler(284, "listen", [&network, descriptor](const A32SyscallFrame& frame) -> A32SyscallOutcome {
        return Done(network.Listen(descriptor(frame, 0), std::bit_cast<std::int32_t>(frame.arguments[1])));
    });
    const auto accept_call = [&network, memory_of, descriptor](const bool with_flags) {
        return [&network, memory_of, descriptor, with_flags](const A32SyscallFrame& frame) -> A32SyscallOutcome {
            net::GuestSocketAddress peer;
            const auto flags = with_flags ? std::bit_cast<std::int32_t>(frame.arguments[3]) : 0;
            const auto memory = memory_of(frame);
            if (frame.arguments[1] != 0 && frame.arguments[2] != 0 && !memory.Word(frame.arguments[2]))
                return Done(-kEfault);
            const auto accepted = network.Accept(descriptor(frame, 0), &peer, flags);
            if (accepted < 0) return Done(accepted);
            if (const auto written = WriteAddress(memory, frame.arguments[1], frame.arguments[2], peer); written < 0)
                return Done(written);
            return Done(accepted);
        };
    };
    bind_handler(285, "accept", accept_call(false));
    bind_handler(366, "accept4", accept_call(true));
    const auto name_call = [&network, memory_of, descriptor](const bool peer) {
        return [&network, memory_of, descriptor, peer](const A32SyscallFrame& frame) -> A32SyscallOutcome {
            net::GuestSocketAddress address;
            const auto fd = descriptor(frame, 0);
            const auto result = peer ? network.PeerName(fd, address) : network.LocalName(fd, address);
            if (result < 0) return Done(result);
            if (frame.arguments[1] == 0 || frame.arguments[2] == 0) return Done(-kEfault);
            return Done(WriteAddress(memory_of(frame), frame.arguments[1], frame.arguments[2], address));
        };
    };
    bind_handler(286, "getsockname", name_call(false));
    bind_handler(287, "getpeername", name_call(true));

    // send(289)/sendto(290): fd, buffer, length, flags[, address, address length].
    const auto send_call = [&network, memory_of, descriptor](const bool to) {
        return [&network, memory_of, descriptor, to](const A32SyscallFrame& frame) -> A32SyscallOutcome {
            const auto memory = memory_of(frame);
            const auto length = std::min<std::size_t>(frame.arguments[2], kMaxTransfer);
            std::vector<std::byte> bytes(length);
            if (!memory.ReadBytes(frame.arguments[1], bytes)) return Done(-kEfault);
            std::optional<net::GuestSocketAddress> destination;
            if (to && frame.arguments[4] != 0) {
                destination.emplace();
                if (const auto read = ReadAddress(memory, frame.arguments[4], frame.arguments[5], *destination);
                    read < 0)
                    return Done(read);
            }
            return Moved(network.Send(descriptor(frame, 0), bytes, std::bit_cast<std::int32_t>(frame.arguments[3]),
                                      destination ? &*destination : nullptr));
        };
    };
    bind_handler(289, "send", send_call(false));
    bind_handler(290, "sendto", send_call(true));
    const auto receive_call = [&network, memory_of, descriptor](const bool from) {
        return [&network, memory_of, descriptor, from](const A32SyscallFrame& frame) -> A32SyscallOutcome {
            const auto memory = memory_of(frame);
            const auto length = std::min<std::size_t>(frame.arguments[2], kMaxTransfer);
            std::vector<std::byte> bytes(length);
            net::GuestSocketAddress source;
            const auto flags = std::bit_cast<std::int32_t>(frame.arguments[3]);
            const auto received = network.Receive(descriptor(frame, 0), bytes, flags, from ? &source : nullptr);
            if (received < 0) return Done(received);
            const auto copied = std::min<std::size_t>(static_cast<std::size_t>(received), bytes.size());
            if (!memory.WriteBytes(frame.arguments[1], std::span(bytes).first(copied))) return Done(-kEfault);
            if (from && source.family != net::kAfUnspec) {
                if (const auto written = WriteAddress(memory, frame.arguments[4], frame.arguments[5], source);
                    written < 0)
                    return Done(written);
            } else if (from && frame.arguments[5] != 0) {
                if (!memory.StoreWord(frame.arguments[5], 0)) return Done(-kEfault);
            }
            return Moved(received);
        };
    };
    bind_handler(291, "recv", receive_call(false));
    bind_handler(292, "recvfrom", receive_call(true));
    bind_handler(293, "shutdown", [&network, descriptor](const A32SyscallFrame& frame) -> A32SyscallOutcome {
        return Done(network.ShutdownSocket(descriptor(frame, 0), std::bit_cast<std::int32_t>(frame.arguments[1])));
    });
    bind_handler(
        294, "setsockopt", [&network, memory_of, descriptor](const A32SyscallFrame& frame) -> A32SyscallOutcome {
            if (frame.arguments[4] > kMaxOption) return Done(-kEinval);
            std::vector<std::byte> value(frame.arguments[4]);
            if (!memory_of(frame).ReadBytes(frame.arguments[3], value)) return Done(-kEfault);
            return Done(network.SetOption(descriptor(frame, 0), std::bit_cast<std::int32_t>(frame.arguments[1]),
                                          std::bit_cast<std::int32_t>(frame.arguments[2]), value));
        });
    bind_handler(
        295, "getsockopt", [&network, memory_of, descriptor](const A32SyscallFrame& frame) -> A32SyscallOutcome {
            const auto memory = memory_of(frame);
            const auto capacity = memory.Word(frame.arguments[4]);
            if (!capacity) return Done(-kEfault);
            if (std::bit_cast<std::int32_t>(*capacity) < 0) return Done(-kEinval);
            std::vector<std::byte> value;
            const auto result = network.GetOption(descriptor(frame, 0), std::bit_cast<std::int32_t>(frame.arguments[1]),
                                                  std::bit_cast<std::int32_t>(frame.arguments[2]), value);
            if (result < 0) return Done(result);
            const auto count = std::min<std::size_t>(*capacity, value.size());
            if (!memory.WriteBytes(frame.arguments[3], std::span(value).first(count)) ||
                !memory.StoreWord(frame.arguments[4], static_cast<std::uint32_t>(count)))
                return Done(-kEfault);
            return Done(0);
        });

    // struct msghdr (ARM): name, namelen, iov, iovlen, control, controllen, flags.
    bind_handler(296, "sendmsg", [&network, memory_of, descriptor](const A32SyscallFrame& frame) -> A32SyscallOutcome {
        const auto memory = memory_of(frame);
        std::array<std::uint32_t, 7> header{};
        for (std::size_t index = 0; index < header.size(); ++index) {
            const auto word = memory.Word(frame.arguments[1] + static_cast<std::uint32_t>(index * 4U));
            if (!word) return Done(-kEfault);
            header[index] = *word;
        }
        // SCM_RIGHTS and other ancillary data are not supported.
        if (header[5] != 0) return Done(-kEopnotsupp);
        const auto vectors = ReadIovecs(memory, header[2], header[3]);
        if (!vectors) return Done(header[3] > kMaxIovecs ? -kEmsgsize : -kEfault);
        std::vector<std::byte> bytes;
        for (const auto& vector : *vectors) {
            const auto take = std::min<std::size_t>(vector.length, kMaxTransfer - bytes.size());
            const auto offset = bytes.size();
            bytes.resize(offset + take);
            if (!memory.ReadBytes(vector.base, std::span(bytes).subspan(offset))) return Done(-kEfault);
            if (bytes.size() == kMaxTransfer) break;
        }
        std::optional<net::GuestSocketAddress> destination;
        if (header[0] != 0 && header[1] != 0) {
            destination.emplace();
            if (const auto read = ReadAddress(memory, header[0], header[1], *destination); read < 0) return Done(read);
        }
        return Moved(network.Send(descriptor(frame, 0), bytes, std::bit_cast<std::int32_t>(frame.arguments[2]),
                                  destination ? &*destination : nullptr));
    });
    bind_handler(297, "recvmsg", [&network, memory_of, descriptor](const A32SyscallFrame& frame) -> A32SyscallOutcome {
        const auto memory = memory_of(frame);
        const auto header_address = frame.arguments[1];
        std::array<std::uint32_t, 7> header{};
        for (std::size_t index = 0; index < header.size(); ++index) {
            const auto word = memory.Word(header_address + static_cast<std::uint32_t>(index * 4U));
            if (!word) return Done(-kEfault);
            header[index] = *word;
        }
        const auto vectors = ReadIovecs(memory, header[2], header[3]);
        if (!vectors) return Done(header[3] > kMaxIovecs ? -kEmsgsize : -kEfault);
        std::size_t capacity{};
        for (const auto& vector : *vectors)
            capacity = std::min(kMaxTransfer, capacity + vector.length);
        std::vector<std::byte> bytes(capacity);
        net::GuestSocketAddress source;
        const auto flags = std::bit_cast<std::int32_t>(frame.arguments[2]);
        const auto received = network.Receive(descriptor(frame, 0), bytes, flags | net::kMsgTrunc, &source);
        if (received < 0) return Done(received);
        const auto available = std::min<std::size_t>(static_cast<std::size_t>(received), bytes.size());
        std::size_t offset{};
        for (const auto& vector : *vectors) {
            if (offset >= available) break;
            const auto take = std::min<std::size_t>(vector.length, available - offset);
            if (!memory.WriteBytes(vector.base, std::span(bytes).subspan(offset, take))) return Done(-kEfault);
            offset += take;
        }
        if (header[0] != 0 && source.family != net::kAfUnspec) {
            const auto encoded = net::EncodeSockaddr(source);
            const auto count = std::min<std::size_t>(header[1], encoded.size());
            if (!memory.WriteBytes(header[0], std::span(encoded).first(count)) ||
                !memory.StoreWord(header_address + 4U, static_cast<std::uint32_t>(encoded.size())))
                return Done(-kEfault);
        } else if (!memory.StoreWord(header_address + 4U, 0)) {
            return Done(-kEfault);
        }
        const bool truncated = static_cast<std::size_t>(received) > bytes.size();
        if (!memory.StoreWord(header_address + 20U, 0) ||
            !memory.StoreWord(header_address + 24U, truncated ? static_cast<std::uint32_t>(net::kMsgTrunc) : 0U))
            return Done(-kEfault);
        // Without MSG_TRUNC in the caller's flags, recvmsg returns the copied size.
        return Moved((flags & net::kMsgTrunc) != 0 ? received : static_cast<std::int64_t>(available));
    });

    // ioctl: FIONREAD and FIONBIO for every descriptor; TCGETS reports "not a tty".
    dispatcher.Implement(54, [&network, memory_of, descriptor](const A32SyscallFrame& frame) -> A32SyscallOutcome {
        constexpr std::uint32_t kTcgets = 0x5401;
        constexpr std::uint32_t kFionread = 0x541b;
        constexpr std::uint32_t kFionbio = 0x5421;
        const auto fd = descriptor(frame, 0);
        const auto memory = memory_of(frame);
        const auto request = frame.arguments[1];
        try {
            auto& filesystem = network.FileSystem();
            if (request == kFionread) {
                const auto available = filesystem.PipeBuffered(fd);
                if (!available) {
                    const auto info = filesystem.DescriptorInfo(fd);
                    if (info.is_directory) return Done(-kEinval);
                    // Regular files: bytes between the offset and EOF are not tracked here.
                    return Done(-kEnotty);
                }
                const auto value = static_cast<std::uint32_t>(std::min<std::size_t>(*available, 0x7fffffffU));
                return Done(memory.StoreWord(frame.arguments[2], value) ? 0 : -kEfault);
            }
            if (request == kFionbio) {
                const auto value = memory.Word(frame.arguments[2]);
                if (!value) return Done(-kEfault);
                filesystem.SetNonBlocking(fd, *value != 0);
                return Done(0);
            }
            if (request == kTcgets) {
                static_cast<void>(filesystem.DescriptorStatus(fd));
                return Done(-kEnotty);
            }
        } catch (const VfsError& error) {
            return Done(-error.ErrorNumber());
        }
        return Done(-kLinuxEnosys);
    });
}

}  // namespace ogplay::runtime
