#include "ogplay/runtime/syscall.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace ogplay::runtime {
namespace {

[[nodiscard]] std::array<std::byte, 4> EncodeWord(
    const std::uint32_t value) {
    std::array<std::byte, 4> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
    return bytes;
}

}  // namespace

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
            if (!frame.cpu_state.has_value() ||
                frame.cpu_state->ThreadId() != frame.thread_id) {
                return -kEinval;
            }
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
                        frame.arguments[4]),
                    *frame.cpu_state};
                const auto result = spawner(request);
                return result == 0 ? -kEagain : result;
            } catch (const std::exception&) {
                return -kEinval;
            }
        });
}

GuestThreadCloneCommitter::GuestThreadCloneCommitter(
    GuestThreadLifecycle& lifecycle, memory::AddressSpace& address_space)
    : lifecycle_(lifecycle), address_space_(address_space) {}

std::int32_t GuestThreadCloneCommitter::Commit(
    const GuestThreadCloneRequest& request,
    const std::uint64_t child_thread_id) {
    constexpr std::int32_t kEsrch = 3;
    constexpr std::int32_t kEagain = 11;
    constexpr std::int32_t kEfault = 14;
    constexpr std::int32_t kEinval = 22;
    constexpr std::int32_t kEoverflow = 75;
    std::scoped_lock lock(mutex_);
    if (child_thread_id == 0) return -kEinval;
    if (child_thread_id > static_cast<std::uint64_t>(
                              std::numeric_limits<std::int32_t>::max())) {
        return -kEoverflow;
    }
    const auto has = [&request](const std::uint32_t flag) {
        return (request.flags & flag) != 0;
    };
    if (request.parent_tid.has_value() != has(kLinuxCloneParentSettid) ||
        request.thread_pointer.has_value() != has(kLinuxCloneSettls) ||
        request.child_tid.has_value() !=
            (has(kLinuxCloneChildSettid) ||
             has(kLinuxCloneChildCleartid))) {
        return -kEinval;
    }
    try {
        if (lifecycle_.State(request.parent_thread_id).status !=
            GuestThreadStatus::running) {
            return -kEsrch;
        }
    } catch (const GuestThreadLifecycleError&) {
        return -kEsrch;
    }
    try {
        static_cast<void>(lifecycle_.State(child_thread_id));
        return -kEagain;
    } catch (const GuestThreadLifecycleError&) {
    }

    std::optional<std::array<std::byte, 4>> original_parent;
    std::optional<std::array<std::byte, 4>> original_child;
    const auto child_value = EncodeWord(
        static_cast<std::uint32_t>(child_thread_id));
    try {
        if (request.parent_tid.has_value()) {
            address_space_.Validate({*request.parent_tid, 4},
                                    memory::AccessType::write,
                                    request.parent_thread_id);
            original_parent.emplace();
            address_space_.Read(*request.parent_tid, *original_parent,
                                request.parent_thread_id);
        }
        if (request.child_tid.has_value()) {
            address_space_.Validate({*request.child_tid, 4},
                                    memory::AccessType::write,
                                    request.parent_thread_id);
            if (!request.parent_tid.has_value() ||
                *request.child_tid != *request.parent_tid) {
                original_child.emplace();
                address_space_.Read(*request.child_tid, *original_child,
                                    request.parent_thread_id);
            }
        }
        if (request.parent_tid.has_value()) {
            address_space_.Write(*request.parent_tid, child_value,
                                 request.parent_thread_id);
        }
        if (has(kLinuxCloneChildSettid)) {
            address_space_.Write(*request.child_tid, child_value,
                                 request.parent_thread_id);
        }
    } catch (const memory::MemoryFault&) {
        if (original_parent.has_value()) {
            try {
                address_space_.Write(*request.parent_tid, *original_parent,
                                     request.parent_thread_id);
            } catch (const memory::MemoryFault&) {
            }
        }
        if (original_child.has_value()) {
            try {
                address_space_.Write(*request.child_tid, *original_child,
                                     request.parent_thread_id);
            } catch (const memory::MemoryFault&) {
            }
        }
        return -kEfault;
    }

    try {
        lifecycle_.RegisterChild(
            request.parent_thread_id, child_thread_id,
            request.thread_pointer.value_or(memory::GuestAddress{0}),
            has(kLinuxCloneChildCleartid)
                ? *request.child_tid
                : memory::GuestAddress{0});
        return static_cast<std::int32_t>(child_thread_id);
    } catch (const GuestThreadLifecycleError&) {
        try {
            if (original_parent.has_value()) {
                address_space_.Write(*request.parent_tid, *original_parent,
                                     request.parent_thread_id);
            }
            if (original_child.has_value()) {
                address_space_.Write(*request.child_tid, *original_child,
                                     request.parent_thread_id);
            }
        } catch (const memory::MemoryFault&) {
        }
        return -kEagain;
    }
}

}  // namespace ogplay::runtime
