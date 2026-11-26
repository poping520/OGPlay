#include "poll_syscalls.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "ogplay/hal/clock.h"

namespace ogplay::runtime {
namespace {

constexpr std::int32_t kEintr = 4;
constexpr std::int32_t kEfault = 14;
constexpr std::int32_t kEinval = 22;

// Linux ARM poll bits.
constexpr std::uint16_t kPollIn = 0x0001;
constexpr std::uint16_t kPollOut = 0x0004;
constexpr std::uint16_t kPollErr = 0x0008;
constexpr std::uint16_t kPollHup = 0x0010;
constexpr std::uint16_t kPollNval = 0x0020;
constexpr std::uint16_t kPollRdNorm = 0x0040;
constexpr std::uint16_t kPollWrNorm = 0x0100;
constexpr std::uint16_t kReadable = kPollIn | kPollRdNorm;
constexpr std::uint16_t kWritable = kPollOut | kPollWrNorm;

// Android's default RLIMIT_NOFILE soft limit; Linux rejects larger nfds with EINVAL.
constexpr std::uint32_t kMaxDescriptors = 1024;
constexpr std::size_t kPollFdBytes = 8;
// Signals and lifecycle exit have no VFS notification; recheck them at this period,
// the same bound GuestSignalRuntime::Sleep uses.
constexpr std::chrono::nanoseconds kInterruptSlice = std::chrono::milliseconds(10);

struct PollEntry final {
    std::int32_t descriptor{};
    std::uint16_t events{};
    std::uint16_t revents{};
};

std::uint32_t Little32(const std::byte* bytes) {
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[index])) << (index * 8U);
    }
    return value;
}

std::uint16_t PipeMask(const std::uint32_t bits) {
    // PipePollEvents: read=1, write=2, error=4, hangup=8, invalid=16.
    if ((bits & 16U) != 0U) return kPollNval;
    std::uint16_t mask{};
    if ((bits & 1U) != 0U) mask |= kReadable;
    if ((bits & 2U) != 0U) mask |= kWritable;
    if ((bits & 4U) != 0U) mask |= kPollErr;
    if ((bits & 8U) != 0U) mask |= kPollHup;
    return mask;
}

// Linux poll mask of one descriptor in the calling thread's table.
std::uint16_t DescriptorMask(VirtualFileSystem& vfs, const GuestPollEndpointQuery& endpoints,
                             const std::int32_t descriptor) {
    if (const auto endpoint = endpoints ? endpoints(descriptor) : std::nullopt) {
        return static_cast<std::uint16_t>((endpoint->read ? kReadable : 0U) | (endpoint->write ? kWritable : 0U));
    }
    const auto bits = vfs.PipePollEvents(descriptor);
    // Regular files, directories and character devices have no poll method:
    // Linux reports DEFAULT_POLLMASK for them.
    if (!bits) return kReadable | kWritable;
    return PipeMask(*bits);
}

// Waits until `ready` reports a count, the timeout expires, a signal is
// pending or the owner's wait limit ends. Shared by select and epoll.
struct WaitResult final {
    std::int32_t value{};
    bool waited{};
    std::chrono::nanoseconds remaining{};
};

template <class Evaluate>
WaitResult WaitReady(VirtualFileSystem& vfs, const GuestPollWaitHooks& hooks, GuestSignalBinding& signals,
                     const std::uint64_t thread_id, const std::optional<std::chrono::nanoseconds> timeout,
                     Evaluate&& evaluate) {
    const auto start = hal::Clock::SteadyTimestampNs();
    std::optional<std::uint64_t> deadline;
    if (timeout) deadline = start + static_cast<std::uint64_t>(timeout->count());
    std::optional<std::uint64_t> limit;
    if (hooks.wait_limit) {
        if (const auto bound = hooks.wait_limit(thread_id))
            limit = start + static_cast<std::uint64_t>(std::max(bound->count(), decltype(bound->count()){0}));
    }
    const auto remaining = [&deadline](const std::uint64_t now) {
        if (!deadline) return std::chrono::nanoseconds{};
        return std::chrono::nanoseconds(*deadline > now ? *deadline - now : 0U);
    };
    bool waited = false;
    for (;;) {
        const auto generation = vfs.PipeReadinessGeneration();
        const auto ready = evaluate();
        const auto now = hal::Clock::SteadyTimestampNs();
        const bool expired = deadline && now >= *deadline;
        if (ready != 0 || (timeout && timeout->count() == 0) || expired) {
            if (expired && ready == 0 && timeout->count() > 0 && hooks.account_timeout)
                hooks.account_timeout(thread_id, *timeout);
            return {ready, waited, remaining(now)};
        }
        if (signals.runtime && signals.runtime->Pending(thread_id)) return {-kEintr, waited, remaining(now)};
        if (limit && now >= *limit) return {0, waited, remaining(now)};
        auto slice = kInterruptSlice;
        if (deadline) slice = std::min(slice, std::chrono::nanoseconds(*deadline - now));
        if (limit) slice = std::min(slice, std::chrono::nanoseconds(*limit - now));
        vfs.WaitPipeReadiness(generation, slice);
        waited = true;
    }
}

// epoll instance: a VFS descriptor object holding the interest list. Like
// Linux, registrations follow descriptor numbers of the creating table;
// a closed descriptor stops reporting. Edge-triggered entries are reported
// level-triggered, which callers that drain to EAGAIN cannot distinguish.
class EpollInstance final : public VfsDescriptorObject {
public:
    struct Interest final {
        std::uint32_t events{};
        std::uint64_t data{};
    };

    std::size_t Read(std::span<std::byte>, const VfsIoContext&) override {
        throw VfsError(kEinval, "epoll descriptor is not readable");
    }

    std::size_t Write(std::span<const std::byte>, const VfsIoContext&) override {
        throw VfsError(kEinval, "epoll descriptor is not writable");
    }

    std::uint32_t PollEvents() const override { return 0; }

    std::mutex mutex;
    std::map<std::int32_t, Interest> interests;
};

class PollCall final {
public:
    PollCall(VirtualFileSystem& vfs, memory::AddressSpace& address_space, const GuestPollEndpointQuery& endpoints,
             const GuestPollWaitHooks& hooks, GuestSignalBinding& signals, const A32SyscallFrame& frame)
        : vfs_(vfs), address_space_(address_space), endpoints_(endpoints), hooks_(hooks), signals_(signals),
          frame_(frame) {}

    struct Result final {
        A32SyscallOutcome outcome;
        std::chrono::nanoseconds remaining{};
    };

    // timeout: nullopt waits indefinitely. Returns remaining time for ppoll write-back.
    Result Run(const memory::GuestAddress table, const std::uint32_t count,
               const std::optional<std::chrono::nanoseconds> timeout) {
        if (count > kMaxDescriptors)
            return {{-kEinval, SupervisorCallProgress::handled_idle}, timeout.value_or(std::chrono::nanoseconds{})};
        std::vector<PollEntry> entries(count);
        try {
            std::vector<std::byte> raw(static_cast<std::size_t>(count) * kPollFdBytes);
            if (!raw.empty()) address_space_.Read(table, raw, frame_.thread_id);
            for (std::size_t index = 0; index < entries.size(); ++index) {
                const auto* entry = raw.data() + index * kPollFdBytes;
                entries[index].descriptor = std::bit_cast<std::int32_t>(Little32(entry));
                entries[index].events = static_cast<std::uint16_t>(Little32(entry + 4) & 0xffffU);
            }
        } catch (const memory::MemoryFault&) {
            return {{-kEfault, SupervisorCallProgress::handled_idle}, timeout.value_or(std::chrono::nanoseconds{})};
        }

        const auto start = hal::Clock::SteadyTimestampNs();
        std::optional<std::uint64_t> deadline;
        if (timeout) deadline = start + static_cast<std::uint64_t>(timeout->count());
        std::optional<std::uint64_t> limit;
        if (hooks_.wait_limit) {
            if (const auto bound = hooks_.wait_limit(frame_.thread_id)) {
                limit = start + static_cast<std::uint64_t>(std::max(bound->count(), decltype(bound->count()){0}));
            }
        }
        const auto remaining = [&deadline](const std::uint64_t now) {
            if (!deadline) return std::chrono::nanoseconds{};
            return std::chrono::nanoseconds(*deadline > now ? *deadline - now : 0U);
        };

        bool waited = false;
        const auto progress = [&waited] {
            return waited ? SupervisorCallProgress::handled_advanced : SupervisorCallProgress::handled_idle;
        };
        for (;;) {
            // Sample before evaluating so a change racing with the scan wakes the wait below.
            const auto generation = vfs_.PipeReadinessGeneration();
            const auto ready = Evaluate(entries);
            const auto now = hal::Clock::SteadyTimestampNs();
            const bool expired = deadline && now >= *deadline;
            if (ready != 0 || (timeout && timeout->count() == 0) || expired) {
                if (expired && ready == 0 && timeout->count() > 0 && hooks_.account_timeout) {
                    hooks_.account_timeout(frame_.thread_id, *timeout);
                }
                if (!Publish(table, entries)) return {{-kEfault, progress()}, remaining(now)};
                return {{ready, progress()}, remaining(now)};
            }
            if (signals_.runtime && signals_.runtime->Pending(frame_.thread_id)) {
                return {{-kEintr, progress()}, remaining(now)};
            }
            if (limit && now >= *limit) {
                // Owner budget ended first: report an ordinary timeout without accounting it.
                if (!Publish(table, entries)) return {{-kEfault, progress()}, remaining(now)};
                return {{0, progress()}, remaining(now)};
            }
            auto slice = kInterruptSlice;
            if (deadline) slice = std::min(slice, std::chrono::nanoseconds(*deadline - now));
            if (limit) slice = std::min(slice, std::chrono::nanoseconds(*limit - now));
            vfs_.WaitPipeReadiness(generation, slice);
            waited = true;
        }
    }

private:

    [[nodiscard]] std::uint16_t Mask(const std::int32_t descriptor) const {
        return DescriptorMask(vfs_, endpoints_, descriptor);
    }

    std::int32_t Evaluate(std::vector<PollEntry>& entries) const {
        std::int32_t ready{};
        for (auto& entry : entries) {
            entry.revents = 0;
            if (entry.descriptor < 0) continue;
            const auto mask = Mask(entry.descriptor);
            entry.revents = static_cast<std::uint16_t>(mask & (entry.events | kPollErr | kPollHup | kPollNval));
            if (entry.revents != 0) ++ready;
        }
        return ready;
    }

    bool Publish(const memory::GuestAddress table, const std::vector<PollEntry>& entries) {
        try {
            for (std::size_t index = 0; index < entries.size(); ++index) {
                const std::array bytes{static_cast<std::byte>(entries[index].revents & 0xffU),
                                       static_cast<std::byte>(entries[index].revents >> 8U)};
                address_space_.Write(table.Add(static_cast<std::uint32_t>(index * kPollFdBytes + 6U)), bytes,
                                     frame_.thread_id);
            }
            return true;
        } catch (const memory::MemoryFault&) {
            return false;
        }
    }

    VirtualFileSystem& vfs_;
    memory::AddressSpace& address_space_;
    const GuestPollEndpointQuery& endpoints_;
    const GuestPollWaitHooks& hooks_;
    GuestSignalBinding& signals_;
    const A32SyscallFrame& frame_;
};

}  // namespace

void BindAndroidPollSyscalls(A32SyscallDispatcher& dispatcher, VirtualFileSystem& vfs,
                             memory::AddressSpace& address_space, GuestPollEndpointQuery endpoints,
                             GuestPollWaitHooks hooks) {
    auto shared =
        std::make_shared<std::pair<GuestPollEndpointQuery, GuestPollWaitHooks>>(std::move(endpoints), std::move(hooks));
    dispatcher.Implement(
        168,
        [&vfs, &address_space, shared,
         signals = dispatcher.signal_binding](const A32SyscallFrame& frame) -> A32SyscallOutcome {
            const auto milliseconds = std::bit_cast<std::int32_t>(frame.arguments[2]);
            std::optional<std::chrono::nanoseconds> timeout;
            if (milliseconds >= 0) timeout = std::chrono::milliseconds(milliseconds);
            PollCall call(vfs, address_space, shared->first, shared->second, *signals, frame);
            return call.Run(memory::GuestAddress{frame.arguments[0]}, frame.arguments[1], timeout).outcome;
        });
    dispatcher.Implement(
        336,
        [&vfs, &address_space, shared,
         signals = dispatcher.signal_binding](const A32SyscallFrame& frame) -> A32SyscallOutcome {
            // A temporary signal mask needs sigsuspend-style swap and restore; not supported.
            if (frame.arguments[3] != 0) return {-kLinuxEnosys, SupervisorCallProgress::handled_idle};
            const memory::GuestAddress timespec{frame.arguments[2]};
            std::optional<std::chrono::nanoseconds> timeout;
            if (frame.arguments[2] != 0) {
                std::array<std::byte, 8> bytes{};
                try {
                    address_space.Read(timespec, bytes, frame.thread_id);
                } catch (const memory::MemoryFault&) {
                    return {-kEfault, SupervisorCallProgress::handled_idle};
                }
                std::uint32_t words[2]{};
                for (std::size_t index = 0; index < bytes.size(); ++index) {
                    words[index / 4] |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[index]))
                                        << ((index % 4) * 8U);
                }
                const auto seconds = std::bit_cast<std::int32_t>(words[0]);
                const auto nanoseconds = std::bit_cast<std::int32_t>(words[1]);
                if (seconds < 0 || nanoseconds < 0 || nanoseconds >= 1'000'000'000) {
                    return {-kEinval, SupervisorCallProgress::handled_idle};
                }
                timeout = std::chrono::seconds(seconds) + std::chrono::nanoseconds(nanoseconds);
            }
            PollCall call(vfs, address_space, shared->first, shared->second, *signals, frame);
            const auto result = call.Run(memory::GuestAddress{frame.arguments[0]}, frame.arguments[1], timeout);
            if (timeout && result.outcome.return_value != -kEfault) {
                // Linux ppoll reports the unslept time; a failed copy does not change the result.
                const auto left = static_cast<std::uint64_t>(result.remaining.count());
                const std::array<std::uint32_t, 2> words{static_cast<std::uint32_t>(left / 1'000'000'000U),
                                                         static_cast<std::uint32_t>(left % 1'000'000'000U)};
                std::array<std::byte, 8> bytes{};
                for (std::size_t index = 0; index < bytes.size(); ++index) {
                    bytes[index] = static_cast<std::byte>((words[index / 4] >> ((index % 4) * 8U)) & 0xffU);
                }
                try {
                    address_space.Write(timespec, bytes, frame.thread_id);
                } catch (const memory::MemoryFault&) {
                }
            }
            return result.outcome;
        });

    const auto read_word = [&address_space](const std::uint32_t address, const std::uint64_t thread,
                                            std::uint32_t& value) {
        std::array<std::byte, 4> bytes{};
        try {
            address_space.Read(memory::GuestAddress{address}, bytes, thread);
        } catch (const memory::MemoryFault&) {
            return false;
        }
        value = Little32(bytes.data());
        return true;
    };
    const auto write_bytes = [&address_space](const std::uint32_t address, const std::span<const std::byte> bytes,
                                              const std::uint64_t thread) {
        try {
            address_space.Write(memory::GuestAddress{address}, bytes, thread);
        } catch (const memory::MemoryFault&) {
            return false;
        }
        return true;
    };

    // _newselect(142): nfds, readfds, writefds, exceptfds, struct timeval*.
    dispatcher.Register(
        142, "_newselect", SyscallGroup::poll,
        [&vfs, &address_space, shared, read_word, write_bytes,
         signals = dispatcher.signal_binding](const A32SyscallFrame& frame) -> A32SyscallOutcome {
            const auto count = std::bit_cast<std::int32_t>(frame.arguments[0]);
            if (count < 0 || static_cast<std::uint32_t>(count) > kMaxDescriptors)
                return {-kEinval, SupervisorCallProgress::handled_idle};
            const auto words = (static_cast<std::uint32_t>(count) + 31U) / 32U;
            std::array<std::vector<std::uint32_t>, 3> sets;
            for (std::size_t set = 0; set < 3; ++set) {
                const auto address = frame.arguments[1 + set];
                if (address == 0) continue;
                sets[set].resize(words);
                for (std::uint32_t word = 0; word < words; ++word)
                    if (!read_word(address + word * 4U, frame.thread_id, sets[set][word]))
                        return {-kEfault, SupervisorCallProgress::handled_idle};
            }
            std::optional<std::chrono::nanoseconds> timeout;
            if (frame.arguments[4] != 0) {
                std::uint32_t seconds{}, micros{};
                if (!read_word(frame.arguments[4], frame.thread_id, seconds) ||
                    !read_word(frame.arguments[4] + 4U, frame.thread_id, micros))
                    return {-kEfault, SupervisorCallProgress::handled_idle};
                if (std::bit_cast<std::int32_t>(seconds) < 0 || std::bit_cast<std::int32_t>(micros) < 0)
                    return {-kEinval, SupervisorCallProgress::handled_idle};
                timeout = std::chrono::seconds(seconds) + std::chrono::microseconds(micros);
            }
            std::array<std::vector<std::uint32_t>, 3> results;
            for (std::size_t set = 0; set < 3; ++set)
                results[set].assign(sets[set].size(), 0U);
            bool bad_descriptor = false;
            const auto evaluate = [&]() -> std::int32_t {
                std::int32_t ready{};
                for (auto& result : results)
                    std::fill(result.begin(), result.end(), 0U);
                for (std::int32_t fd = 0; fd < count; ++fd) {
                    const auto word = static_cast<std::uint32_t>(fd) / 32U;
                    const auto bit = 1U << (static_cast<std::uint32_t>(fd) % 32U);
                    const bool wanted[3] = {!sets[0].empty() && (sets[0][word] & bit) != 0,
                                            !sets[1].empty() && (sets[1][word] & bit) != 0,
                                            !sets[2].empty() && (sets[2][word] & bit) != 0};
                    if (!wanted[0] && !wanted[1] && !wanted[2]) continue;
                    const auto mask = DescriptorMask(vfs, shared->first, fd);
                    if ((mask & kPollNval) != 0) {
                        bad_descriptor = true;
                        return -1;
                    }
                    // Linux POLLIN_SET / POLLOUT_SET / POLLEX_SET.
                    if (wanted[0] && (mask & (kReadable | kPollHup | kPollErr)) != 0) {
                        results[0][word] |= bit;
                        ++ready;
                    }
                    if (wanted[1] && (mask & (kWritable | kPollErr)) != 0) {
                        results[1][word] |= bit;
                        ++ready;
                    }
                }
                return ready;
            };
            const auto waited = WaitReady(vfs, shared->second, *signals, frame.thread_id, timeout, evaluate);
            const auto progress =
                waited.waited ? SupervisorCallProgress::handled_advanced : SupervisorCallProgress::handled_idle;
            if (bad_descriptor) return {-9, progress};
            if (waited.value < 0) return {waited.value, progress};
            for (std::size_t set = 0; set < 3; ++set) {
                const auto address = frame.arguments[1 + set];
                if (address == 0) continue;
                std::vector<std::byte> bytes(results[set].size() * 4U);
                for (std::size_t word = 0; word < results[set].size(); ++word)
                    for (std::size_t index = 0; index < 4; ++index)
                        bytes[word * 4 + index] = static_cast<std::byte>((results[set][word] >> (index * 8U)) & 0xffU);
                if (!write_bytes(address, bytes, frame.thread_id)) return {-kEfault, progress};
            }
            if (timeout) {
                // Linux select writes back the unslept time.
                const auto left = static_cast<std::uint64_t>(waited.remaining.count());
                std::array<std::byte, 8> bytes{};
                const std::array<std::uint32_t, 2> value{static_cast<std::uint32_t>(left / 1'000'000'000U),
                                                         static_cast<std::uint32_t>((left % 1'000'000'000U) / 1000U)};
                for (std::size_t index = 0; index < bytes.size(); ++index)
                    bytes[index] = static_cast<std::byte>((value[index / 4] >> ((index % 4) * 8U)) & 0xffU);
                static_cast<void>(write_bytes(frame.arguments[4], bytes, frame.thread_id));
            }
            static_cast<void>(address_space);
            return {waited.value, progress};
        });

    const auto create_epoll = [&vfs](const bool close_on_exec) -> std::int32_t {
        try {
            const auto descriptor = vfs.OpenObject(std::make_shared<EpollInstance>(), {.read = true, .write = true});
            if (close_on_exec) vfs.SetCloseOnExec(descriptor, true);
            return descriptor;
        } catch (const VfsError& error) {
            return -error.ErrorNumber();
        }
    };
    dispatcher.Implement(250, [create_epoll](const A32SyscallFrame& frame) -> A32SyscallOutcome {
        if (std::bit_cast<std::int32_t>(frame.arguments[0]) <= 0)
            return {-kEinval, SupervisorCallProgress::handled_idle};
        return {create_epoll(false), SupervisorCallProgress::handled_idle};
    });
    dispatcher.Implement(357, [create_epoll](const A32SyscallFrame& frame) -> A32SyscallOutcome {
        constexpr std::uint32_t kEpollCloexec = 0x80000;
        if ((frame.arguments[0] & ~kEpollCloexec) != 0) return {-kEinval, SupervisorCallProgress::handled_idle};
        return {create_epoll(frame.arguments[0] != 0), SupervisorCallProgress::handled_idle};
    });
    const auto epoll_of = [&vfs](const std::int32_t descriptor, std::int32_t& error) {
        std::shared_ptr<EpollInstance> instance;
        try {
            instance = std::dynamic_pointer_cast<EpollInstance>(vfs.DescriptorObject(descriptor));
            error = instance ? 0 : -kEinval;
        } catch (const VfsError& failure) {
            error = -failure.ErrorNumber();
        }
        return instance;
    };
    // struct epoll_event on ARM EABI: u32 events, 4 bytes padding, u64 data (16 bytes).
    dispatcher.Implement(
        251, [&vfs, &address_space, epoll_of, shared](const A32SyscallFrame& frame) -> A32SyscallOutcome {
            constexpr std::uint32_t kAdd = 1, kDel = 2, kMod = 3;
            const auto epfd = std::bit_cast<std::int32_t>(frame.arguments[0]);
            const auto op = frame.arguments[1];
            const auto fd = std::bit_cast<std::int32_t>(frame.arguments[2]);
            std::int32_t error{};
            const auto instance = epoll_of(epfd, error);
            if (!instance) return {error, SupervisorCallProgress::handled_idle};
            if (fd == epfd) return {-kEinval, SupervisorCallProgress::handled_idle};
            const auto mask = DescriptorMask(vfs, shared->first, fd);
            if ((mask & kPollNval) != 0) return {-9, SupervisorCallProgress::handled_idle};
            // Linux rejects regular files and directories with EPERM.
            if (!shared->first || !shared->first(fd).has_value()) {
                try {
                    if (!vfs.PipePollEvents(fd).has_value()) return {-1, SupervisorCallProgress::handled_idle};
                } catch (const VfsError& failure) {
                    return {-failure.ErrorNumber(), SupervisorCallProgress::handled_idle};
                }
            }
            EpollInstance::Interest interest;
            if (op != kDel) {
                std::array<std::byte, 16> bytes{};
                try {
                    address_space.Read(memory::GuestAddress{frame.arguments[3]}, bytes, frame.thread_id);
                } catch (const memory::MemoryFault&) {
                    return {-kEfault, SupervisorCallProgress::handled_idle};
                }
                interest.events = Little32(bytes.data());
                interest.data = static_cast<std::uint64_t>(Little32(bytes.data() + 8)) |
                                (static_cast<std::uint64_t>(Little32(bytes.data() + 12)) << 32U);
            }
            std::scoped_lock lock(instance->mutex);
            const auto found = instance->interests.find(fd);
            if (op == kAdd) {
                if (found != instance->interests.end()) return {-17, SupervisorCallProgress::handled_idle};
                instance->interests.emplace(fd, interest);
            } else if (op == kMod) {
                if (found == instance->interests.end()) return {-2, SupervisorCallProgress::handled_idle};
                found->second = interest;
            } else if (op == kDel) {
                if (found == instance->interests.end()) return {-2, SupervisorCallProgress::handled_idle};
                instance->interests.erase(found);
            } else {
                return {-kEinval, SupervisorCallProgress::handled_idle};
            }
            vfs.NotifyReadiness();
            return {0, SupervisorCallProgress::handled_idle};
        });
    const auto epoll_wait = [&vfs, &address_space, epoll_of, shared](const A32SyscallFrame& frame,
                                                                     GuestSignalBinding& signals) -> A32SyscallOutcome {
        constexpr std::uint32_t kEpollOneShot = 1U << 30U;
        constexpr std::uint32_t kEpollRdHup = 0x2000;
        const auto epfd = std::bit_cast<std::int32_t>(frame.arguments[0]);
        const auto maximum = std::bit_cast<std::int32_t>(frame.arguments[2]);
        if (maximum <= 0 || maximum > 1 << 20) return {-kEinval, SupervisorCallProgress::handled_idle};
        std::int32_t error{};
        const auto instance = epoll_of(epfd, error);
        if (!instance) return {error, SupervisorCallProgress::handled_idle};
        const auto milliseconds = std::bit_cast<std::int32_t>(frame.arguments[3]);
        std::optional<std::chrono::nanoseconds> timeout;
        if (milliseconds >= 0) timeout = std::chrono::milliseconds(milliseconds);
        std::vector<std::pair<std::uint32_t, std::uint64_t>> events;
        const auto evaluate = [&]() -> std::int32_t {
            events.clear();
            std::scoped_lock lock(instance->mutex);
            for (auto& [fd, interest] : instance->interests) {
                if (events.size() >= static_cast<std::size_t>(maximum)) break;
                if ((interest.events & ~(kEpollOneShot | (1U << 31U))) == 0) continue;
                const auto mask = DescriptorMask(vfs, shared->first, fd);
                if ((mask & kPollNval) != 0) continue;
                std::uint32_t revents = mask & (interest.events | kPollErr | kPollHup);
                if ((mask & kPollHup) != 0 && (interest.events & kEpollRdHup) != 0) revents |= kEpollRdHup;
                if (revents == 0) continue;
                events.emplace_back(revents, interest.data);
                if ((interest.events & kEpollOneShot) != 0) interest.events &= kEpollOneShot | (1U << 31U);
            }
            return static_cast<std::int32_t>(events.size());
        };
        const auto waited = WaitReady(vfs, shared->second, signals, frame.thread_id, timeout, evaluate);
        const auto progress =
            waited.waited ? SupervisorCallProgress::handled_advanced : SupervisorCallProgress::handled_idle;
        if (waited.value <= 0) return {waited.value, progress};
        std::vector<std::byte> bytes(events.size() * 16U);
        for (std::size_t index = 0; index < events.size(); ++index) {
            const auto [revents, data] = events[index];
            for (std::size_t byte = 0; byte < 4; ++byte)
                bytes[index * 16 + byte] = static_cast<std::byte>((revents >> (byte * 8U)) & 0xffU);
            for (std::size_t byte = 0; byte < 8; ++byte)
                bytes[index * 16 + 8 + byte] = static_cast<std::byte>((data >> (byte * 8U)) & 0xffU);
        }
        try {
            address_space.Write(memory::GuestAddress{frame.arguments[1]}, bytes, frame.thread_id);
        } catch (const memory::MemoryFault&) {
            return {-kEfault, progress};
        }
        return {waited.value, progress};
    };
    dispatcher.Implement(252, [epoll_wait, signals = dispatcher.signal_binding](const A32SyscallFrame& frame) {
        return epoll_wait(frame, *signals);
    });
    dispatcher.Implement(346, [epoll_wait, signals = dispatcher.signal_binding](const A32SyscallFrame& frame) {
        // A temporary signal mask needs sigsuspend-style swap and restore; not supported.
        if (frame.arguments[4] != 0) return A32SyscallOutcome{-kLinuxEnosys, SupervisorCallProgress::handled_idle};
        return epoll_wait(frame, *signals);
    });
}

}  // namespace ogplay::runtime
