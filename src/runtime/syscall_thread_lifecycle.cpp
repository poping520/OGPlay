#include "ogplay/runtime/syscall.h"

#include <bit>
#include <cstdint>
#include <limits>

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

}  // namespace ogplay::runtime
