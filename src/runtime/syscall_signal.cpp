#include "ogplay/runtime/syscall.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <unordered_map>

namespace ogplay::runtime {
namespace {

constexpr std::int32_t kEsrch = 3;
constexpr std::int32_t kEfault = 14;
constexpr std::int32_t kEinval = 22;
constexpr std::int32_t kEnomem = 12;
constexpr std::uint32_t kSigBlock = 0;
constexpr std::uint32_t kSigUnblock = 1;
constexpr std::uint32_t kSigSetmask = 2;
constexpr std::uint64_t kUnblockableSignals =
    (UINT64_C(1) << (9U - 1U)) | (UINT64_C(1) << (19U - 1U));

struct AlternateStack final {
    std::uint32_t pointer{};
    std::uint32_t flags{2};
    std::uint32_t size{};
};

struct SignalState final {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::uint64_t> masks;
    std::unordered_map<std::uint64_t, AlternateStack> alternate_stacks;
};

[[nodiscard]] std::uint32_t DecodeWord(
    const std::span<const std::byte, 4> bytes) {
    std::uint32_t result{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[index]))
                  << static_cast<unsigned>(index * 8U);
    }
    return result;
}

[[nodiscard]] std::array<std::byte, 4> EncodeWord(
    const std::uint32_t value) {
    std::array<std::byte, 4> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
    return result;
}

[[nodiscard]] std::uint64_t ReadMask(memory::AddressSpace& address_space,
                                     const memory::GuestAddress address,
                                     const std::size_t size,
                                     const std::uint64_t thread_id) {
    std::array<std::byte, 8> bytes{};
    address_space.Read(address, std::span{bytes}.first(size), thread_id);
    const auto low = DecodeWord(std::span<const std::byte, 4>{bytes.data(), 4});
    if (size == 4) return low;
    const auto high = DecodeWord(
        std::span<const std::byte, 4>{bytes.data() + 4, 4});
    return static_cast<std::uint64_t>(low) |
           (static_cast<std::uint64_t>(high) << 32U);
}

void WriteMask(memory::AddressSpace& address_space,
               const memory::GuestAddress address, const std::uint64_t mask,
               const std::size_t size, const std::uint64_t thread_id) {
    const auto low = EncodeWord(static_cast<std::uint32_t>(mask));
    address_space.Write(address, low, thread_id);
    if (size == 8) {
        const auto high = EncodeWord(static_cast<std::uint32_t>(mask >> 32U));
        address_space.Write(address.Add(4), high, thread_id);
    }
}

[[nodiscard]] std::uint64_t UpdatedMask(const std::uint64_t previous,
                                        const std::uint64_t requested,
                                        const std::uint32_t how) {
    std::uint64_t result{};
    if (how == kSigBlock) {
        result = previous | requested;
    } else if (how == kSigUnblock) {
        result = previous & ~requested;
    } else if (how == kSigSetmask) {
        result = requested;
    } else {
        throw std::invalid_argument("invalid signal mask operation");
    }
    return result & ~kUnblockableSignals;
}

}  // namespace

void BindAndroidSignalSyscalls(A32SyscallDispatcher& dispatcher,
                               memory::AddressSpace& address_space) {
    const auto state = std::make_shared<SignalState>();
    const auto bind_mask = [&dispatcher, &address_space, state](
                               const std::uint32_t number,
                               const std::size_t set_size,
                               const bool has_size_argument) {
        dispatcher.Implement(
            number, [&address_space, state, set_size, has_size_argument](
                        const A32SyscallFrame& frame) {
                if (frame.thread_id == 0) return -kEsrch;
                if (has_size_argument && frame.arguments[3] != set_size) {
                    return -kEinval;
                }
                try {
                    const auto set = memory::GuestAddress{frame.arguments[1]};
                    const auto old = memory::GuestAddress{frame.arguments[2]};
                    if (set.Value() != 0 && frame.arguments[0] > kSigSetmask) {
                        return -kEinval;
                    }
                    if (set.Value() != 0) {
                        address_space.Validate({set, set_size},
                                               memory::AccessType::read,
                                               frame.thread_id);
                    }
                    if (old.Value() != 0) {
                        address_space.Validate({old, set_size},
                                               memory::AccessType::write,
                                               frame.thread_id);
                    }
                    const auto requested = set.Value() == 0
                                               ? UINT64_C(0)
                                               : ReadMask(address_space, set,
                                                          set_size,
                                                          frame.thread_id);
                    std::scoped_lock lock(state->mutex);
                    const auto previous = state->masks[frame.thread_id];
                    if (old.Value() != 0) {
                        WriteMask(address_space, old, previous, set_size,
                                  frame.thread_id);
                    }
                    if (set.Value() != 0) {
                        state->masks[frame.thread_id] = UpdatedMask(
                            previous, requested, frame.arguments[0]);
                    }
                    return 0;
                } catch (const memory::MemoryFault&) {
                    return -kEfault;
                } catch (const std::exception&) {
                    return -kEinval;
                }
            });
    };
    bind_mask(126, 4, false);
    bind_mask(175, 8, true);

    dispatcher.Implement(
        186, [&address_space, state](const A32SyscallFrame& frame) {
            if (frame.thread_id == 0) return -kEsrch;
            try {
                const auto input = memory::GuestAddress{frame.arguments[0]};
                const auto output = memory::GuestAddress{frame.arguments[1]};
                if (input.Value() != 0) {
                    address_space.Validate({input, 12},
                                           memory::AccessType::read,
                                           frame.thread_id);
                }
                if (output.Value() != 0) {
                    address_space.Validate({output, 12},
                                           memory::AccessType::write,
                                           frame.thread_id);
                }
                AlternateStack requested;
                if (input.Value() != 0) {
                    std::array<std::byte, 12> bytes{};
                    address_space.Read(input, bytes, frame.thread_id);
                    requested.pointer = DecodeWord(
                        std::span<const std::byte, 4>{bytes.data(), 4});
                    requested.flags = DecodeWord(
                        std::span<const std::byte, 4>{bytes.data() + 4, 4});
                    requested.size = DecodeWord(
                        std::span<const std::byte, 4>{bytes.data() + 8, 4});
                    if (requested.flags != 0 && requested.flags != 2) {
                        return -kEinval;
                    }
                    if (requested.flags == 0 &&
                        (requested.pointer == 0 || requested.size < 2048U)) {
                        return -kEnomem;
                    }
                }
                std::scoped_lock lock(state->mutex);
                const auto previous = state->alternate_stacks.contains(
                                          frame.thread_id)
                                          ? state->alternate_stacks.at(
                                                frame.thread_id)
                                          : AlternateStack{};
                if (output.Value() != 0) {
                    const std::array encoded{
                        EncodeWord(previous.pointer),
                        EncodeWord(previous.flags),
                        EncodeWord(previous.size)};
                    address_space.Write(output, encoded[0], frame.thread_id);
                    address_space.Write(output.Add(4), encoded[1],
                                        frame.thread_id);
                    address_space.Write(output.Add(8), encoded[2],
                                        frame.thread_id);
                }
                if (input.Value() != 0) {
                    state->alternate_stacks[frame.thread_id] = requested;
                }
                return 0;
            } catch (const memory::MemoryFault&) {
                return -kEfault;
            } catch (const std::exception&) {
                return -kEinval;
            }
        });
}

}  // namespace ogplay::runtime
