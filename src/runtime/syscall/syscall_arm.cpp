#include "ogplay/runtime/syscall/syscall.h"

#include <cstdint>
#include <exception>
#include <utility>

namespace ogplay::runtime {

void BindAndroidArmPrivateSyscalls(
    A32SyscallDispatcher& dispatcher,
    GuestThreadPointerSetter thread_pointer_setter) {
    if (!thread_pointer_setter) {
        throw SyscallError("ARM set_tls thread pointer setter is empty");
    }
    constexpr std::uint32_t kArmSetTls = 0x0f0005U;
    constexpr std::int32_t kEsrch = 3;
    constexpr std::int32_t kEinval = 22;
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
