#include "poll_syscalls.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
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
    static std::uint32_t Little32(const std::byte* bytes) {
        std::uint32_t value{};
        for (std::size_t index = 0; index < 4; ++index) {
            value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[index])) << (index * 8U);
        }
        return value;
    }

    [[nodiscard]] std::uint16_t Mask(const std::int32_t descriptor) const {
        if (const auto endpoint = endpoints_ ? endpoints_(descriptor) : std::nullopt) {
            return static_cast<std::uint16_t>((endpoint->read ? kReadable : 0U) | (endpoint->write ? kWritable : 0U));
        }
        const auto bits = vfs_.PipePollEvents(descriptor);
        // Regular files, directories and character devices have no poll method:
        // Linux reports DEFAULT_POLLMASK for them.
        if (!bits) return kReadable | kWritable;
        return PipeMask(*bits);
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
}

}  // namespace ogplay::runtime
