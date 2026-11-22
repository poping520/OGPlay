#include "ogplay/runtime/syscall/syscall.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ogplay/runtime/process/virtual_process.h"
#include "guest_path_reader.h"

namespace ogplay::runtime {
namespace {

constexpr std::int32_t kEperm = 1;
constexpr std::int32_t kEsrch = 3;
constexpr std::int32_t kEfault = 14;
constexpr std::int32_t kEinval = 22;
constexpr std::int32_t kE2big = 7;
constexpr std::uint32_t kWaitNoHang = 1;
// struct rusage on 32-bit ARM: two timevals and fourteen longs.
constexpr std::size_t kRusageBytes = 72;
constexpr std::size_t kMaximumVectorEntries = 1024;
constexpr std::size_t kMaximumStringBytes = 128U * 1024U;
// API 19 pthread_internal_t: TLS slot 1 points at it; attr.stack_base and
// attr.stack_size sit at offsets 12 and 16.
constexpr std::uint32_t kTlsThreadSlotOffset = 4;
constexpr std::uint32_t kThreadStackBaseOffset = 12;
constexpr std::uint32_t kThreadStackSizeOffset = 16;
constexpr std::uint64_t kMaximumForkStackBytes = 8U * 1024U * 1024U;

// The parent's live stack, [sp, stack top), as it was at fork(2).
struct StackSnapshot final {
    memory::GuestAddress start;
    std::vector<std::byte> bytes;
};

// A vfork child running on its parent's thread until execve or _exit.
struct VforkFrame final {
    std::int32_t child{};
    cpu::A32State parent_state;
    std::uint64_t parent_mask{};
    std::optional<StackSnapshot> parent_stack;
};

struct SpawnState final {
    explicit SpawnState(VirtualProcessKernel& owner) : kernel(owner) {}

    [[nodiscard]] std::optional<std::int32_t> Child(const std::uint64_t thread) const {
        std::scoped_lock lock(mutex);
        const auto found = frames.find(thread);
        if (found == frames.end() || found->second.empty()) return std::nullopt;
        return found->second.back().child;
    }

    [[nodiscard]] std::int32_t CurrentPid(const std::uint64_t thread) const {
        return Child(thread).value_or(kernel.ApplicationPid());
    }

    VirtualProcessKernel& kernel;
    mutable std::mutex mutex;
    std::unordered_map<std::uint64_t, std::vector<VforkFrame>> frames;
};

[[nodiscard]] std::string ReadGuestString(memory::AddressSpace& address_space, const std::uint32_t raw_address,
                                          const std::uint64_t thread, std::size_t& budget) {
    std::string value;
    auto address = memory::GuestAddress{raw_address};
    for (;;) {
        if (budget == 0) throw VirtualProcessError(kE2big, "exec strings exceed the argument budget");
        std::array<std::byte, 1> byte{};
        address_space.Read(address, byte, thread);
        --budget;
        const auto character = std::to_integer<char>(byte[0]);
        if (character == '\0') return value;
        value.push_back(character);
        address = address.Add(1);
    }
}

[[nodiscard]] std::vector<std::string> ReadGuestVector(memory::AddressSpace& address_space,
                                                       const std::uint32_t raw_address, const std::uint64_t thread,
                                                       std::size_t& budget) {
    std::vector<std::string> values;
    if (raw_address == 0) return values;
    for (std::size_t index = 0;; ++index) {
        if (index == kMaximumVectorEntries) throw VirtualProcessError(kE2big, "too many exec vector entries");
        std::array<std::byte, 4> encoded{};
        address_space.Read(memory::GuestAddress{raw_address}.Add(index * 4U), encoded, thread);
        std::uint32_t pointer{};
        for (std::size_t byte = 0; byte < encoded.size(); ++byte)
            pointer |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(encoded[byte])) << (byte * 8U);
        if (pointer == 0) return values;
        values.push_back(ReadGuestString(address_space, pointer, thread, budget));
    }
}

[[nodiscard]] std::uint32_t ReadGuest32(memory::AddressSpace& address_space, const std::uint32_t raw_address,
                                        const std::uint64_t thread) {
    std::array<std::byte, 4> encoded{};
    address_space.Read(memory::GuestAddress{raw_address}, encoded, thread);
    std::uint32_t value{};
    for (std::size_t byte = 0; byte < encoded.size(); ++byte)
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(encoded[byte])) << (byte * 8U);
    return value;
}

// A fork(2) child returns out of libc fork() and its later calls reuse the
// stack below the caller's frame, where fork() saved its registers and
// return address. Copying the thread's used stack lets the parent resume
// with the frames it had. The bounds come from the bionic thread record;
// without a plausible one nothing is copied.
[[nodiscard]] std::optional<StackSnapshot> CaptureParentStack(memory::AddressSpace& address_space,
                                                              const cpu::A32State& state, const std::uint64_t thread) {
    try {
        const auto sp = state.Register(cpu::CoreRegister::sp);
        const auto tls = state.ThreadPointer().Value();
        if (tls == 0) return std::nullopt;
        const auto record = ReadGuest32(address_space, tls + kTlsThreadSlotOffset, thread);
        if (record == 0) return std::nullopt;
        const std::uint64_t base = ReadGuest32(address_space, record + kThreadStackBaseOffset, thread);
        const auto top = base + ReadGuest32(address_space, record + kThreadStackSizeOffset, thread);
        if (sp < base || sp >= top || top > memory::kGuestAddressSpaceSize || top - sp > kMaximumForkStackBytes)
            return std::nullopt;
        StackSnapshot snapshot{memory::GuestAddress{sp}, std::vector<std::byte>(top - sp)};
        address_space.Read(snapshot.start, snapshot.bytes, thread);
        return snapshot;
    } catch (const memory::MemoryFault&) {
        return std::nullopt;
    } catch (const std::overflow_error&) {
        return std::nullopt;
    }
}

void WriteZeroes(memory::AddressSpace& address_space, const std::uint32_t raw_address, const std::size_t size,
                 const std::uint64_t thread) {
    std::vector<std::byte> zeroes(size);
    address_space.Write(memory::GuestAddress{raw_address}, zeroes, thread);
}

}  // namespace

void BindAndroidProcessSpawnSyscalls(A32SyscallDispatcher& dispatcher, memory::AddressSpace& address_space,
                                     VirtualProcessKernel& kernel) {
    const auto state = std::make_shared<SpawnState>(kernel);
    const auto signals = dispatcher.signal_binding;
    const auto filesystem = dispatcher.filesystem_binding;

    *dispatcher.descriptor_table_binding = [state](const std::uint64_t thread) {
        const auto child = state->Child(thread);
        if (!child) return kVfsApplicationDescriptorTable;
        try {
            return state->kernel.DescriptorTable(*child);
        } catch (const VirtualProcessError&) {
            return kVfsApplicationDescriptorTable;
        }
    };

    // Ends child mode on `thread`: the parent continues after its vfork with
    // the child's pid in r0 and its own signal mask.
    const auto resume_parent = [state, signals, &address_space, &kernel](const std::uint64_t thread) {
        VforkFrame frame;
        {
            std::scoped_lock lock(state->mutex);
            auto& stack = state->frames.at(thread);
            frame = std::move(stack.back());
            stack.pop_back();
            if (stack.empty()) state->frames.erase(thread);
        }
        if (signals->runtime) signals->runtime->SetMask(thread, frame.parent_mask);
        if (frame.parent_stack) {
            try {
                address_space.Write(frame.parent_stack->start, frame.parent_stack->bytes, thread);
            } catch (const memory::MemoryFault&) {
                kernel.RecordUnsupported("syscall.fork.stack_restore");
            }
        }
        A32SyscallOutcome outcome;
        auto restored = frame.parent_state;
        restored.SetRegister(cpu::CoreRegister::r0, static_cast<std::uint32_t>(frame.child));
        outcome.restored_state = restored;
        outcome.return_value = frame.child;
        outcome.progress = SupervisorCallProgress::handled_advanced;
        return outcome;
    };

    const auto fork = [state, signals, &address_space, &kernel](const A32SyscallFrame& frame) -> A32SyscallOutcome {
        if (!frame.cpu_state || frame.thread_id == 0) return -kEinval;
        const auto parent = state->CurrentPid(frame.thread_id);
        std::int32_t child{};
        try {
            const auto table =
                parent == kernel.ApplicationPid() ? kVfsApplicationDescriptorTable : kernel.DescriptorTable(parent);
            child = kernel.Fork(parent, table);
        } catch (const VirtualProcessError& error) {
            return -error.ErrorNumber();
        } catch (const VfsError& error) {
            return -error.ErrorNumber();
        }
        // fork(2) runs the child like vfork on the one guest address space,
        // but gives the parent back its own stack. Heap and global writes the
        // child makes before exec stay visible to the parent.
        std::optional<StackSnapshot> stack;
        if (frame.number == 2) {
            kernel.RecordUnsupported("syscall.fork.shared_memory");
            stack = CaptureParentStack(address_space, *frame.cpu_state, frame.thread_id);
            if (!stack) kernel.RecordUnsupported("syscall.fork.unknown_stack");
        }
        const auto mask = signals->runtime ? signals->runtime->Mask(frame.thread_id) : 0U;
        std::scoped_lock lock(state->mutex);
        state->frames[frame.thread_id].push_back({child, *frame.cpu_state, mask, std::move(stack)});
        return 0;
    };
    dispatcher.Implement(2, fork);
    dispatcher.Implement(190, fork);

    dispatcher.Implement(
        11,
        [state, filesystem, resume_parent, &address_space, &kernel](const A32SyscallFrame& frame) -> A32SyscallOutcome {
            const auto child = state->Child(frame.thread_id);
            // Replacing the application process image itself is not supported.
            if (!child) return -kLinuxEnosys;
            try {
                auto path = syscall_detail::ReadGuestPath(address_space, frame.arguments[0]);
                std::size_t budget = kMaximumStringBytes;
                auto arguments = ReadGuestVector(address_space, frame.arguments[1], frame.thread_id, budget);
                auto environment = ReadGuestVector(address_space, frame.arguments[2], frame.thread_id, budget);
                std::string working_directory = "/";
                if (auto* vfs = *filesystem; vfs != nullptr) working_directory = vfs->WorkingDirectory().value_or("/");
                if (!path.starts_with('/'))
                    path = (working_directory.ends_with('/') ? working_directory : working_directory + "/") + path;
                kernel.Exec(*child, path, std::move(arguments), std::move(environment), std::move(working_directory));
            } catch (const memory::MemoryFault&) {
                return -kEfault;
            } catch (const std::overflow_error&) {
                return -kEfault;
            } catch (const VfsError& error) {
                return -error.ErrorNumber();
            } catch (const VirtualProcessError& error) {
                return -error.ErrorNumber();
            }
            return resume_parent(frame.thread_id);
        });

    const auto exit_child = [state, resume_parent, &kernel](const A32SyscallFrame& frame,
                                                            const A32SyscallDispatcher::Handler& previous) {
        const auto child = state->Child(frame.thread_id);
        if (!child) return previous(frame);
        kernel.Exit(*child, std::bit_cast<std::int32_t>(frame.arguments[0]));
        return resume_parent(frame.thread_id);
    };
    dispatcher.Decorate(1, exit_child);
    dispatcher.Decorate(248, exit_child);
    dispatcher.Decorate(20, [state](const A32SyscallFrame& frame, const A32SyscallDispatcher::Handler& previous) {
        const auto child = state->Child(frame.thread_id);
        return child ? A32SyscallOutcome{*child} : previous(frame);
    });
    dispatcher.Decorate(64,
                        [state, &kernel](const A32SyscallFrame& frame, const A32SyscallDispatcher::Handler& previous) {
                            const auto child = state->Child(frame.thread_id);
                            return child ? A32SyscallOutcome{kernel.ParentOf(*child)} : previous(frame);
                        });

    dispatcher.Implement(114, [state, signals, &address_space, &kernel](const A32SyscallFrame& frame) {
        const auto caller = state->CurrentPid(frame.thread_id);
        const auto selector = std::bit_cast<std::int32_t>(frame.arguments[0]);
        const auto no_hang = (frame.arguments[2] & kWaitNoHang) != 0;
        VirtualProcessWaitResult result;
        try {
            result = kernel.Wait(caller, selector > 0 ? selector : -1, no_hang, [signals, thread = frame.thread_id] {
                return signals->runtime && signals->runtime->Pending(thread);
            });
        } catch (const VirtualProcessError& error) {
            if (error.ErrorNumber() == 4 && signals->runtime) signals->runtime->InterruptedWait(frame);
            return A32SyscallOutcome{-error.ErrorNumber()};
        }
        try {
            if (result.pid != 0 && frame.arguments[1] != 0) {
                const auto status = static_cast<std::uint32_t>(result.status);
                const std::array bytes{
                    static_cast<std::byte>(status & 0xffU), static_cast<std::byte>((status >> 8U) & 0xffU),
                    static_cast<std::byte>((status >> 16U) & 0xffU), static_cast<std::byte>((status >> 24U) & 0xffU)};
                address_space.Write(memory::GuestAddress{frame.arguments[1]}, bytes, frame.thread_id);
            }
            if (frame.arguments[3] != 0) WriteZeroes(address_space, frame.arguments[3], kRusageBytes, frame.thread_id);
        } catch (const memory::MemoryFault&) {
            return A32SyscallOutcome{-kEfault};
        }
        return A32SyscallOutcome{result.pid, result.pid != 0 ? SupervisorCallProgress::handled_advanced
                                                             : SupervisorCallProgress::handled_idle};
    });

    dispatcher.Implement(37, [state, &dispatcher, &kernel](const A32SyscallFrame& frame) -> A32SyscallOutcome {
        const auto pid = std::bit_cast<std::int32_t>(frame.arguments[0]);
        const auto signal = std::bit_cast<std::int32_t>(frame.arguments[1]);
        if (signal < 0 || signal > 64) return -kEinval;
        const auto self = state->CurrentPid(frame.thread_id);
        const auto application = kernel.ApplicationPid();
        if (pid < -1) return -kEsrch;
        if (pid == -1) return -kEperm;
        if ((pid == 0 || pid == application) && self == application) {
            // kill(getpid()) goes to a thread that may take it: the caller.
            if (signal == 0) return 0;
            A32SyscallFrame targeted = frame;
            targeted.number = 268;
            targeted.arguments = {static_cast<std::uint32_t>(application), static_cast<std::uint32_t>(frame.thread_id),
                                  static_cast<std::uint32_t>(signal)};
            return dispatcher.DispatchOutcome(targeted);
        }
        try {
            kernel.Kill(pid == 0 ? self : pid, signal);
            return 0;
        } catch (const VirtualProcessError& error) {
            return -error.ErrorNumber();
        }
    });
}

}  // namespace ogplay::runtime
