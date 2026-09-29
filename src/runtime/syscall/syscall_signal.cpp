#include "ogplay/runtime/syscall/syscall.h"

#include <array>
#include <algorithm>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <utility>
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

struct SignalAction final {
    memory::GuestAddress handler{0};
    std::uint32_t flags{};
    memory::GuestAddress restorer{0};
    std::uint64_t mask{};
};
struct SignalFrame final {
    memory::GuestAddress stack;
    memory::GuestAddress context;
    bool realtime{};
    cpu::A32State saved;
};
struct SignalData {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::uint64_t> masks;
    std::unordered_map<std::uint64_t, AlternateStack> alternate_stacks;
    std::array<SignalAction, 65> actions{};
    std::unordered_map<std::uint64_t, std::uint64_t> pending;
    std::unordered_map<std::uint64_t, std::array<A32SyscallFrame, 32>> senders;
    std::unordered_map<std::uint64_t, std::vector<SignalFrame>> frames;
    std::unordered_map<std::uint64_t, std::uint64_t> suspend_masks;
    std::unordered_map<std::uint64_t, A32SyscallFrame> restart;
    std::condition_variable changed;
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

class GuestSignalRuntime::Impl final : public SignalData {
public:
    Impl(memory::AddressSpace& space, GuestThreadLifecycle* threads, std::uint32_t pid, std::uint32_t uid)
        : memory(space), lifecycle(threads), process_id(pid), user_id(uid) {}
    memory::AddressSpace& memory;
    GuestThreadLifecycle* lifecycle;
    std::uint32_t process_id;
    std::uint32_t user_id;
    memory::GuestAddress trampoline{0};
    A32SyscallOutcome Action(const A32SyscallFrame&);
    A32SyscallOutcome Send(const A32SyscallFrame&);
    A32SyscallOutcome Suspend(const A32SyscallFrame&);
    A32SyscallOutcome Return(const A32SyscallFrame&);
    bool Ready(std::uint64_t id) { return (pending[id] & ~masks[id]) != 0; }
};

GuestSignalRuntime::GuestSignalRuntime(memory::AddressSpace& memory,
                                       GuestThreadLifecycle* lifecycle,
                                       std::uint32_t process_id, std::uint32_t user_id)
    : impl_(std::make_shared<Impl>(memory, lifecycle, process_id, user_id)) {}
GuestSignalRuntime::~GuestSignalRuntime() = default;

void BindAndroidSignalSyscalls(A32SyscallDispatcher& dispatcher,
                               memory::AddressSpace& address_space,
                               GuestThreadLifecycle* lifecycle) {
    dispatcher.signal_binding->runtime = std::make_shared<GuestSignalRuntime>(
        address_space, lifecycle, dispatcher.identity.process_id, dispatcher.identity.user_id);
    dispatcher.signal_binding->runtime->Bind(dispatcher);
}

void GuestSignalRuntime::Bind(A32SyscallDispatcher& dispatcher) {
    auto& address_space = impl_->memory;
    const auto state = impl_;
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
                auto previous = state->alternate_stacks.contains(
                                          frame.thread_id)
                                          ? state->alternate_stacks.at(
                                                frame.thread_id)
                                          : AlternateStack{};
                if (frame.cpu_state && previous.flags != 2) {
                    const auto sp = frame.cpu_state->Register(cpu::CoreRegister::sp);
                    if (sp >= previous.pointer && static_cast<std::uint64_t>(sp) <
                            static_cast<std::uint64_t>(previous.pointer) + previous.size) {
                        if (input.Value() != 0) return -1; // EPERM while on alternate stack
                        previous.flags = 1;
                    }
                }
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

    for (const auto number : {67U, 174U}) {
        dispatcher.Implement(number, [state](const A32SyscallFrame& frame) {
            return state->Action(frame);
        });
    }
    for (const auto number : {72U, 179U}) {
        dispatcher.Implement(number, [state](const A32SyscallFrame& frame) {
            return state->Suspend(frame);
        });
    }
    for (const auto number : {119U, 173U}) {
        dispatcher.Implement(number, [state](const A32SyscallFrame& frame) {
            return state->Return(frame);
        });
    }
    for (const auto number : {73U, 176U}) {
        dispatcher.Implement(number, [state](const A32SyscallFrame& frame) -> A32SyscallOutcome {
            if (frame.thread_id == 0) return -kEsrch;
            const std::size_t size = frame.number == 73 ? 4 : 8;
            if (size == 8 && frame.arguments[1] != 8) return -kEinval;
            try {
                std::scoped_lock lock(state->mutex);
                const memory::GuestAddress output{frame.arguments[0]};
                state->memory.Validate({output, size}, memory::AccessType::write, frame.thread_id);
                WriteMask(state->memory, output,
                          state->pending[frame.thread_id] & state->masks[frame.thread_id], size, frame.thread_id);
                return 0;
            } catch (const memory::MemoryFault&) { return -kEfault; }
        });
    }
    dispatcher.Implement(268, [state, binding = dispatcher.signal_binding](const A32SyscallFrame& frame) {
        auto result = state->Send(frame);
        // Notify after releasing signal state; futex predicates take its lock.
        if (result.return_value == 0 && binding->waiters) {
            binding->waiters->NotifyWaiters();
        }
        return result;
    });
}

A32SyscallOutcome GuestSignalRuntime::Impl::Action(const A32SyscallFrame& frame) {
    const auto signal = frame.arguments[0];
    if (signal == 0 || signal > 64 || signal == 9 || signal == 19) return -kEinval;
    const bool rt = frame.number == 174;
    if (rt && frame.arguments[3] != 8) return -kEinval;
    const auto size = rt ? 20U : 16U;
    try {
        const memory::GuestAddress input{frame.arguments[1]}, output{frame.arguments[2]};
        SignalAction requested;
        if (!input.IsNull()) {
            memory.Validate({input, size}, memory::AccessType::read, frame.thread_id);
            requested.handler = memory::GuestAddress{memory.Read32(input, frame.thread_id)};
            requested.flags = memory.Read32(input.Add(rt ? 4 : 8), frame.thread_id);
            requested.restorer = memory::GuestAddress{memory.Read32(input.Add(rt ? 8 : 12), frame.thread_id)};
            requested.mask = ReadMask(memory, input.Add(rt ? 12 : 4), rt ? 8 : 4, frame.thread_id) & ~kUnblockableSignals;
            constexpr std::uint32_t supported = 0xdc000004U; // INFO, RESTORER, ONSTACK, RESTART, NODEFER, RESETHAND
            if ((requested.flags & ~supported) != 0) return -95;
        }
        if (!output.IsNull()) memory.Validate({output, size}, memory::AccessType::write, frame.thread_id);
        std::scoped_lock lock(mutex);
        const auto old = actions[signal];
        if (!output.IsNull()) {
            memory.Write32(output, old.handler.Value(), frame.thread_id);
            memory.Write32(output.Add(rt ? 4 : 8), old.flags, frame.thread_id);
            memory.Write32(output.Add(rt ? 8 : 12), old.restorer.Value(), frame.thread_id);
            WriteMask(memory, output.Add(rt ? 12 : 4), old.mask, rt ? 8 : 4, frame.thread_id);
        }
        if (!input.IsNull()) {
            actions[signal] = requested;
            if (requested.handler.Value() == 1) {
                const auto bit = UINT64_C(1) << (signal - 1);
                for (auto& item : pending) item.second &= ~bit;
            }
        }
        return 0;
    } catch (const memory::MemoryFault&) { return -kEfault; }
}

A32SyscallOutcome GuestSignalRuntime::Impl::Send(const A32SyscallFrame& frame) {
    const auto target = static_cast<std::uint64_t>(frame.arguments[1]);
    const auto signal = frame.arguments[2];
    if (signal > 64) return -kEinval;
    if (!lifecycle || !frame.thread_id || !target || frame.arguments[0] != process_id) return -kEsrch;
    try {
        if (lifecycle->State(target).status != GuestThreadStatus::running) return -kEsrch;
        if (signal == 0) return 0;
        std::scoped_lock lock(mutex);
        // Standard signals coalesce; realtime queues and job control are not implemented.
        if (signal >= 32 || signal == 19 || signal == 18 || (signal >= 20 && signal <= 22)) return -kLinuxEnosys;
        const auto action = actions[signal];
        if (action.handler.Value() == 1) return 0;
        if (action.handler.IsNull()) {
            if (signal == 17 || signal == 23 || signal == 28) return 0;
            if (signal == 9 || !(masks[target] & (UINT64_C(1) << (signal - 1)))) {
                lifecycle->RequestExitGroup(frame.thread_id, 128 + static_cast<std::int32_t>(signal),
                    {.origin = GuestThreadExitOrigin::signal_termination,
                     .requesting_thread_id = frame.thread_id, .syscall_number = frame.number,
                     .program_counter = frame.program_counter, .link_register = frame.link_register,
                     .signal_number = signal, .target_thread_id = target});
                changed.notify_all();
                return 0;
            }
        }
        const auto bit = UINT64_C(1) << (signal - 1);
        if (!(pending[target] & bit)) senders[target][signal] = frame;
        pending[target] |= bit;
        changed.notify_all();
        return 0;
    } catch (const GuestThreadLifecycleError&) { return -kEsrch; }
}

bool GuestSignalRuntime::Pending(const std::uint64_t id) const {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->lifecycle) {
        try {
            if (impl_->lifecycle->State(id).status != GuestThreadStatus::running) return true;
        } catch (const GuestThreadLifecycleError&) { return true; }
    }
    return impl_->Ready(id);
}
void GuestSignalRuntime::Inherit(const std::uint64_t parent, const std::uint64_t child) {
    std::scoped_lock lock(impl_->mutex);
    impl_->masks[child] = impl_->masks[parent];
}
void GuestSignalRuntime::Retire(const std::uint64_t id) {
    std::scoped_lock lock(impl_->mutex);
    impl_->masks.erase(id); impl_->alternate_stacks.erase(id);
    impl_->pending.erase(id); impl_->senders.erase(id); impl_->frames.erase(id);
    impl_->suspend_masks.erase(id); impl_->restart.erase(id);
}
void GuestSignalRuntime::InterruptedWait(const A32SyscallFrame& frame) {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->Ready(frame.thread_id) && frame.cpu_state && frame.arguments[3] == 0) {
        impl_->restart[frame.thread_id] = frame;
    }
}

A32SyscallOutcome GuestSignalRuntime::Impl::Suspend(const A32SyscallFrame& frame) {
    if (!frame.thread_id || !lifecycle) return -kEsrch;
    try {
        const auto requested = frame.number == 72 ? frame.arguments[2] :
            (frame.arguments[1] == 8 ? ReadMask(memory, memory::GuestAddress{frame.arguments[0]}, 8, frame.thread_id) : UINT64_MAX);
        if (frame.number == 179 && frame.arguments[1] != 8) return -kEinval;
        std::unique_lock lock(mutex);
        const auto old = masks[frame.thread_id];
        masks[frame.thread_id] = requested & ~kUnblockableSignals;
        while (!Ready(frame.thread_id)) {
            if (lifecycle->State(frame.thread_id).status != GuestThreadStatus::running) {
                masks[frame.thread_id] = old;
                return -4;
            }
            // Bounded cancellation polling; no guest timeout or new time source.
            changed.wait_for(lock, std::chrono::milliseconds(10));
        }
        suspend_masks[frame.thread_id] = old;
        return {-4, SupervisorCallProgress::handled_advanced};
    } catch (const memory::MemoryFault&) { return -kEfault; }
      catch (const GuestThreadLifecycleError&) { return -kEsrch; }
}

void GuestSignalRuntime::Deliver(cpu::Cpu& cpu) {
    auto saved = cpu.GetState();
    const auto id = saved.ThreadId();
    auto& s = *impl_;
    std::scoped_lock lock(s.mutex);
    if (!s.Ready(id)) {
        s.restart.erase(id);
        if (const auto found = s.suspend_masks.find(id); found != s.suspend_masks.end()) {
            s.masks[id] = found->second;
            s.suspend_masks.erase(found);
        }
        return;
    }
    if (s.lifecycle && s.lifecycle->State(id).status != GuestThreadStatus::running) return;
    const auto bits = s.pending[id] & ~s.masks[id];
    const auto signal = static_cast<std::uint32_t>(std::countr_zero(bits)) + 1U;
    const auto bit = UINT64_C(1) << (signal - 1);
    const auto action = s.actions[signal];
    const auto sender = s.senders[id][signal];
    if (action.handler.Value() == 1) { s.pending[id] &= ~bit; return; }
    if (action.handler.IsNull()) {
        s.pending[id] &= ~bit;
        if (signal == 17 || signal == 23 || signal == 28) return;
        if (!s.lifecycle) throw SyscallError("signal default termination requires lifecycle");
        s.lifecycle->RequestExitGroup(id, 128 + static_cast<std::int32_t>(signal),
            {.origin = GuestThreadExitOrigin::signal_termination,
             .requesting_thread_id = sender.thread_id, .syscall_number = sender.number,
             .program_counter = sender.program_counter, .link_register = sender.link_register,
             .signal_number = signal, .target_thread_id = id});
        return;
    }
    if (s.frames[id].size() >= 32) throw SyscallError("guest signal nesting limit exceeded");
    if (const auto found = s.restart.find(id); found != s.restart.end()) {
        if ((action.flags & 0x10000000U) != 0) {
            saved = *found->second.cpu_state;
            saved.SetRegister(cpu::CoreRegister::pc, found->second.program_counter);
        }
        s.restart.erase(found);
    }
    const auto old_mask = s.suspend_masks.contains(id) ? s.suspend_masks.at(id) : s.masks[id];
    const auto alt = s.alternate_stacks.contains(id) ? s.alternate_stacks.at(id) : AlternateStack{};
    const auto sp = saved.Register(cpu::CoreRegister::sp);
    const bool on_alt = alt.flags != 2 && sp >= alt.pointer && static_cast<std::uint64_t>(sp) < static_cast<std::uint64_t>(alt.pointer) + alt.size;
    const auto top = ((action.flags & 0x08000000U) && alt.flags != 2 && !on_alt)
        ? static_cast<std::uint64_t>(alt.pointer) + alt.size : sp;
    const bool rt = (action.flags & 4) != 0;
    constexpr std::uint32_t context_size = 744;
    const auto prefix = rt ? 128U : 0U;
    const auto size = prefix + context_size + 8U;
    if (top < size || top > UINT32_MAX) throw SyscallError("guest signal stack overflow");
    const memory::GuestAddress stack{static_cast<std::uint32_t>((top - size) & ~UINT64_C(7))};
    if (((action.flags & 0x08000000U) || on_alt) && alt.flags != 2 && stack.Value() < alt.pointer)
        throw SyscallError("guest alternate signal stack overflow");
    const auto context = stack.Add(prefix);
    std::vector<std::byte> bytes(size);
    auto word = [&](std::uint32_t offset, std::uint32_t value) {
        const auto encoded = EncodeWord(value);
        std::copy(encoded.begin(), encoded.end(), bytes.begin() + offset);
    };
    if (rt) {
        word(0, signal); word(8, static_cast<std::uint32_t>(-6)); // SI_TKILL
        word(12, s.process_id); word(16, s.user_id);
    }
    word(prefix + 8, alt.pointer); word(prefix + 12, on_alt ? 1U : alt.flags); word(prefix + 16, alt.size);
    word(prefix + 28, static_cast<std::uint32_t>(old_mask));
    for (std::uint32_t i = 0; i < 16; ++i) word(prefix + 32 + i * 4, saved.Register(static_cast<cpu::CoreRegister>(i)));
    word(prefix + 96, saved.Cpsr());
    word(prefix + 104, static_cast<std::uint32_t>(old_mask)); word(prefix + 108, static_cast<std::uint32_t>(old_mask >> 32));
    // ARM ucontext.uc_regspace: Linux VFP context followed by the end marker.
    word(prefix + 232, 0x56465001); word(prefix + 236, 288);
    for (std::uint32_t i = 0; i < 64; ++i) word(prefix + 240 + i * 4, saved.ExtendedRegisters()[i]);
    word(prefix + 496, saved.Fpscr()); word(prefix + 504, saved.Fpexc());
    if (s.trampoline.IsNull()) {
        s.trampoline = s.memory.MapAnywhere({memory::GuestAddress{0x7e000000}, 0x01000000},
            s.memory.PageSize(), memory::PageProtection::read | memory::PageProtection::write);
        s.memory.Write32(s.trampoline, 0xe3a07077); // mov r7, #119
        s.memory.Write32(s.trampoline.Add(4), 0xef000000);
        s.memory.Write32(s.trampoline.Add(8), 0xe3a070ad); // mov r7, #173
        s.memory.Write32(s.trampoline.Add(12), 0xef000000);
        s.memory.Protect({s.trampoline, s.memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::execute);
    }
    const auto restorer = (action.flags & 0x04000000U) ? action.restorer : s.trampoline.Add(rt ? 8 : 0);
    s.memory.Validate({memory::GuestAddress{action.handler.Value() & ~1U}, 2}, memory::AccessType::execute, id);
    s.memory.Validate({memory::GuestAddress{restorer.Value() & ~1U}, 2}, memory::AccessType::execute, id);
    s.memory.Write(stack, bytes, id);
    s.frames[id].push_back({stack, context, rt, saved});
    s.suspend_masks.erase(id);
    s.pending[id] &= ~bit;
    s.masks[id] = (s.masks[id] | action.mask | ((action.flags & 0x40000000U) ? 0 : bit)) & ~kUnblockableSignals;
    if (action.flags & 0x80000000U) s.actions[signal] = {};
    auto next = saved;
    next.SetRegister(cpu::CoreRegister::r0, signal);
    if (rt) { next.SetRegister(cpu::CoreRegister::r1, stack.Value()); next.SetRegister(cpu::CoreRegister::r2, context.Value()); }
    next.SetRegister(cpu::CoreRegister::sp, stack.Value()); next.SetRegister(cpu::CoreRegister::lr, restorer.Value());
    next.SetCpsr(saved.Cpsr() & ~0x0600fc00U); // clear Thumb IT state
    next.SetState((action.handler.Value() & 1) ? cpu::ExecutionState::thumb : cpu::ExecutionState::a32);
    next.SetRegister(cpu::CoreRegister::pc, action.handler.Value() & ~1U);
    cpu.SetState(next);
}

A32SyscallOutcome GuestSignalRuntime::Impl::Return(const A32SyscallFrame& frame) {
    std::scoped_lock lock(mutex);
    auto& active = frames[frame.thread_id];
    if (!frame.cpu_state || active.empty()) throw SyscallError("sigreturn without active signal frame");
    const auto record = active.back();
    if (record.stack.Value() != frame.cpu_state->Register(cpu::CoreRegister::sp) || record.realtime != (frame.number == 173))
        throw SyscallError("sigreturn stack or ABI mismatch");
    auto restored = record.saved;
    for (std::uint32_t i = 0; i < 16; ++i)
        restored.SetRegister(static_cast<cpu::CoreRegister>(i), memory.Read32(record.context.Add(32 + i * 4), frame.thread_id));
    const auto cpsr = memory.Read32(record.context.Add(96), frame.thread_id);
    if ((cpsr & 0x1f) != 0x10 || (cpsr & 0xc0)) throw SyscallError("sigreturn invalid user CPSR");
    restored.SetCpsr(cpsr);
    const auto alignment = restored.State() == cpu::ExecutionState::thumb ? 1U : 3U;
    if ((restored.Register(cpu::CoreRegister::pc) & alignment) != 0)
        throw SyscallError("sigreturn unaligned program counter");
    if (memory.Read32(record.context.Add(232), frame.thread_id) != 0x56465001 || memory.Read32(record.context.Add(236), frame.thread_id) != 288)
        throw SyscallError("sigreturn invalid VFP context");
    for (std::uint8_t i = 0; i < 64; ++i)
        restored.SetExtendedRegister(i, memory.Read32(record.context.Add(240 + static_cast<std::uint32_t>(i) * 4), frame.thread_id));
    restored.SetFpscr(memory.Read32(record.context.Add(496), frame.thread_id));
    restored.SetFpexc(memory.Read32(record.context.Add(504), frame.thread_id));
    const auto mask = ReadMask(memory, record.context.Add(104), 8, frame.thread_id);
    if (record.realtime) {
        const auto pointer = memory.Read32(record.context.Add(8), frame.thread_id);
        const auto flags = memory.Read32(record.context.Add(12), frame.thread_id);
        const auto size = memory.Read32(record.context.Add(16), frame.thread_id);
        if (flags > 2 || (flags != 2 && (pointer == 0 || size < 2048 ||
            static_cast<std::uint64_t>(pointer) + size > UINT32_MAX)))
            throw SyscallError("sigreturn invalid alternate stack");
        alternate_stacks[frame.thread_id] = {pointer, flags == 2 ? 2U : 0U, size};
    }
    masks[frame.thread_id] = mask & ~kUnblockableSignals;
    active.pop_back();
    A32SyscallOutcome outcome{std::bit_cast<std::int32_t>(restored.Register(cpu::CoreRegister::r0))};
    outcome.restored_state = restored;
    return outcome;
}

} // namespace ogplay::runtime
