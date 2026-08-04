#include "ogplay/runtime/syscall.h"

#include <bit>
#include <cstdint>
#include <limits>
#include <utility>

namespace ogplay::runtime {

void BindAndroidThreadLifecycleSyscalls(
    A32SyscallDispatcher& dispatcher,
    GuestThreadLifecycle& thread_lifecycle) {
    constexpr std::int32_t kEsrch = 3;
    constexpr std::int32_t kEoverflow = 75;
    dispatcher.Implement(
        256, [&thread_lifecycle](const A32SyscallFrame& frame) {
            if (frame.thread_id == 0) return -kEsrch;
            if (frame.thread_id > static_cast<std::uint64_t>(
                                      std::numeric_limits<std::int32_t>::max())) {
                return -kEoverflow;
            }
            try {
                thread_lifecycle.SetClearChildTid(
                    frame.thread_id,
                    memory::GuestAddress{frame.arguments[0]});
                return static_cast<std::int32_t>(frame.thread_id);
            } catch (const GuestThreadLifecycleError&) {
                return -kEsrch;
            }
        });
    dispatcher.Implement(
        1, [&thread_lifecycle](const A32SyscallFrame& frame) {
            try {
                thread_lifecycle.RequestExit(
                    frame.thread_id,
                    std::bit_cast<std::int32_t>(frame.arguments[0]));
                return 0;
            } catch (const GuestThreadLifecycleError&) {
                return -kEsrch;
            }
        });
    dispatcher.Implement(
        248, [&thread_lifecycle](const A32SyscallFrame& frame) {
            try {
                thread_lifecycle.RequestExitGroup(
                    frame.thread_id,
                    std::bit_cast<std::int32_t>(frame.arguments[0]));
                return 0;
            } catch (const GuestThreadLifecycleError&) {
                return -kEsrch;
            }
        });
}

void BindAndroidCloneSyscall(A32SyscallDispatcher& dispatcher,
                             GuestThreadCloneSpawner spawner) {
    constexpr std::int32_t kEsrch = 3;
    constexpr std::int32_t kEagain = 11;
    constexpr std::int32_t kEinval = 22;
    constexpr std::int32_t kEnotsup = 95;
    constexpr std::uint32_t kRequiredFlags =
        kLinuxCloneVm | kLinuxCloneFs | kLinuxCloneFiles |
        kLinuxCloneSighand | kLinuxCloneThread;
    constexpr std::uint32_t kAllowedFlags =
        kRequiredFlags | kLinuxCloneSysvsem | kLinuxCloneSettls |
        kLinuxCloneParentSettid | kLinuxCloneChildCleartid |
        kLinuxCloneChildSettid;
    if (!spawner) throw SyscallError("clone spawner is empty");
    dispatcher.Implement(
        120, [spawner = std::move(spawner)](const A32SyscallFrame& frame) {
            if (frame.thread_id == 0) return -kEsrch;
            const auto flags = frame.arguments[0];
            if ((flags & kRequiredFlags) != kRequiredFlags ||
                (flags & ~kAllowedFlags) != 0) {
                return -kEnotsup;
            }
            const auto child_stack = frame.arguments[1];
            if (child_stack == 0 || child_stack % 16U != 0) return -kEinval;
            const auto optional_pointer = [](const bool present,
                                             const std::uint32_t value) {
                if (!present) {
                    return std::optional<memory::GuestAddress>{};
                }
                if (value == 0 || value % alignof(std::uint32_t) != 0) {
                    throw std::invalid_argument("invalid clone pointer");
                }
                return std::optional{
                    memory::GuestAddress{value}};
            };
            try {
                GuestThreadCloneRequest request{
                    frame.thread_id,
                    flags,
                    memory::GuestAddress{child_stack},
                    optional_pointer((flags & kLinuxCloneParentSettid) != 0,
                                     frame.arguments[2]),
                    optional_pointer((flags & kLinuxCloneSettls) != 0,
                                     frame.arguments[3]),
                    optional_pointer(
                        (flags & (kLinuxCloneChildSettid |
                                  kLinuxCloneChildCleartid)) != 0,
                        frame.arguments[4])};
                const auto result = spawner(request);
                return result == 0 ? -kEagain : result;
            } catch (const std::exception&) {
                return -kEinval;
            }
        });
}

}  // namespace ogplay::runtime
