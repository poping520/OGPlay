#include "ogplay/runtime/syscall/syscall.h"

#include <cstdint>
#include <exception>
#include <utility>

namespace ogplay::runtime {

void BindAndroidArmPrivateSyscalls(
    A32SyscallDispatcher& dispatcher,
    memory::AddressSpace& address_space,
    GuestThreadPointerSetter thread_pointer_setter) {
    if (!thread_pointer_setter) {
        throw SyscallError("ARM set_tls thread pointer setter is empty");
    }
    constexpr std::uint32_t kArmSetTls = 0x0f0005U;
    constexpr std::uint32_t kArmCacheflush = 0x0f0002U;
    constexpr std::int32_t kEfault = 14;
    constexpr std::int32_t kEsrch = 3;
    constexpr std::int32_t kEinval = 22;
    dispatcher.Implement(
        kArmCacheflush,
        [&address_space](const A32SyscallFrame& frame) {
            const auto start = frame.arguments[0];
            const auto end = frame.arguments[1];
            if (end < start || frame.arguments[2] != 0) return -kEinval;
            if (end == start) return 0;
            try {
                address_space.ValidateMapped(
                    {memory::GuestAddress{start},
                     static_cast<std::uint64_t>(end) - start},
                    frame.thread_id);
                // The interpreter fetches guest bytes directly and has no host
                // instruction cache or translated-code cache to invalidate.
                return 0;
            } catch (const memory::MemoryFault&) {
                return -kEfault;
            } catch (const std::exception&) {
                return -kEinval;
            }
        });
    dispatcher.Implement(
        kArmSetTls,
        [setter = std::move(thread_pointer_setter)](
            const A32SyscallFrame& frame) {
            if (frame.thread_id == 0) return -kEsrch;
            try {
                const auto updated = setter(
                    frame.thread_id,
                    memory::GuestAddress{frame.arguments[0]});
                return updated ? 0 : -kEsrch;
            } catch (const std::exception&) {
                return -kEinval;
            }
        });
}

}  // namespace ogplay::runtime
