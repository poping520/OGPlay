#include "ogplay/cpu/dynarmic.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include <dynarmic/interface/A32/a32.h>

namespace ogplay::cpu {
namespace {

inline constexpr std::uint32_t kThumbBit = 1U << 5U;
inline constexpr Dynarmic::HaltReason kCallbackHalt =
    Dynarmic::HaltReason::UserDefined1;
inline constexpr Dynarmic::HaltReason kExternalHalt =
    Dynarmic::HaltReason::UserDefined2;

struct PendingStop final {
    RunStopReason reason{RunStopReason::undefined_instruction};
    memory::GuestAddress pc;
    std::uint32_t instruction{};
    std::uint32_t immediate{};
    std::optional<CpuFault> fault;
};

}  // namespace

class DynarmicCpu::Impl final {
public:
    class Callbacks final : public Dynarmic::A32::UserCallbacks {
    public:
        explicit Callbacks(memory::MemoryBus& memory_bus) noexcept
            : memory_bus_(memory_bus) {}

        void Attach(Dynarmic::A32::Jit& jit) noexcept { jit_ = &jit; }

        void Begin(const std::uint64_t tick_budget,
                   const std::uint64_t thread_id) noexcept {
            ticks_remaining_ = tick_budget;
            ticks_consumed_ = 0;
            thread_id_ = thread_id;
            pending_.reset();
        }

        [[nodiscard]] std::uint64_t TicksConsumed() const noexcept {
            return ticks_consumed_;
        }

        [[nodiscard]] const std::optional<PendingStop>& Pending() const noexcept {
            return pending_;
        }

        std::optional<std::uint32_t> MemoryReadCode(
            const Dynarmic::A32::VAddr address) override {
            try {
                return memory_bus_.Fetch32(memory::GuestAddress{address}, thread_id_);
            } catch (const memory::MemoryFault& fault) {
                RecordFault(fault);
                return std::nullopt;
            }
        }

        std::uint8_t MemoryRead8(const Dynarmic::A32::VAddr address) override {
            return Read<std::uint8_t>(address, &memory::MemoryBus::Read8);
        }
        std::uint16_t MemoryRead16(const Dynarmic::A32::VAddr address) override {
            return Read<std::uint16_t>(address, &memory::MemoryBus::Read16);
        }
        std::uint32_t MemoryRead32(const Dynarmic::A32::VAddr address) override {
            return Read<std::uint32_t>(address, &memory::MemoryBus::Read32);
        }
        std::uint64_t MemoryRead64(const Dynarmic::A32::VAddr address) override {
            return Read<std::uint64_t>(address, &memory::MemoryBus::Read64);
        }

        void MemoryWrite8(const Dynarmic::A32::VAddr address,
                          const std::uint8_t value) override {
            Write(address, value, &memory::MemoryBus::Write8);
        }
        void MemoryWrite16(const Dynarmic::A32::VAddr address,
                           const std::uint16_t value) override {
            Write(address, value, &memory::MemoryBus::Write16);
        }
        void MemoryWrite32(const Dynarmic::A32::VAddr address,
                           const std::uint32_t value) override {
            Write(address, value, &memory::MemoryBus::Write32);
        }
        void MemoryWrite64(const Dynarmic::A32::VAddr address,
                           const std::uint64_t value) override {
            Write(address, value, &memory::MemoryBus::Write64);
        }

        void InterpreterFallback(const Dynarmic::A32::VAddr pc,
                                 std::size_t) override {
            RecordStop({RunStopReason::undefined_instruction,
                        memory::GuestAddress{pc}});
        }

        void CallSVC(const std::uint32_t immediate) override {
            const bool thumb = (jit_->Cpsr() & kThumbBit) != 0;
            const auto instruction_size = thumb ? 2U : 4U;
            const auto next_pc = jit_->Regs()[15];
            const auto instruction = thumb ? 0xdf00U | (immediate & 0xffU)
                                           : 0xef000000U | (immediate & 0x00ffffffU);
            RecordStop({RunStopReason::supervisor_call,
                        memory::GuestAddress{next_pc - instruction_size},
                        instruction, immediate});
        }

        void ExceptionRaised(const Dynarmic::A32::VAddr pc,
                             const Dynarmic::A32::Exception exception) override {
            const auto reason = exception == Dynarmic::A32::Exception::Breakpoint
                                    ? RunStopReason::breakpoint
                                    : RunStopReason::undefined_instruction;
            RecordStop({reason, memory::GuestAddress{pc}});
        }

        void AddTicks(const std::uint64_t ticks) override {
            const auto consumed = std::min(ticks, ticks_remaining_);
            ticks_consumed_ += consumed;
            ticks_remaining_ -= consumed;
        }

        std::uint64_t GetTicksRemaining() override { return ticks_remaining_; }

    private:
        template <typename UInt>
        using ReadFunction = UInt (memory::MemoryBus::*)(memory::GuestAddress,
                                                         std::uint64_t);

        template <typename UInt>
        [[nodiscard]] UInt Read(const Dynarmic::A32::VAddr address,
                                const ReadFunction<UInt> function) {
            try {
                return (memory_bus_.*function)(memory::GuestAddress{address}, thread_id_);
            } catch (const memory::MemoryFault& fault) {
                RecordFault(fault);
                return 0;
            }
        }

        template <typename UInt>
        using WriteFunction = void (memory::MemoryBus::*)(memory::GuestAddress, UInt,
                                                          std::uint64_t);

        template <typename UInt>
        void Write(const Dynarmic::A32::VAddr address, const UInt value,
                   const WriteFunction<UInt> function) {
            try {
                (memory_bus_.*function)(memory::GuestAddress{address}, value, thread_id_);
            } catch (const memory::MemoryFault& fault) {
                RecordFault(fault);
            }
        }

        void RecordFault(const memory::MemoryFault& fault) {
            if (!pending_.has_value()) {
                pending_ = PendingStop{
                    RunStopReason::memory_fault,
                    memory::GuestAddress{jit_->Regs()[15]},
                    0,
                    0,
                    CpuFault{fault.Address(), fault.Access(), fault.Reason(),
                             fault.ThreadId()}};
            }
            jit_->HaltExecution(kCallbackHalt);
        }

        void RecordStop(PendingStop stop) {
            if (!pending_.has_value()) pending_ = std::move(stop);
            jit_->HaltExecution(kCallbackHalt);
        }

        memory::MemoryBus& memory_bus_;
        Dynarmic::A32::Jit* jit_{};
        std::uint64_t thread_id_{};
        std::uint64_t ticks_remaining_{};
        std::uint64_t ticks_consumed_{};
        std::optional<PendingStop> pending_;
    };

    explicit Impl(memory::MemoryBus& memory_bus)
        : callbacks(memory_bus), jit(MakeConfig(callbacks)) {
        callbacks.Attach(jit);
    }

    static Dynarmic::A32::UserConfig MakeConfig(Callbacks& callbacks) {
        Dynarmic::A32::UserConfig config{&callbacks};
        config.arch_version = Dynarmic::A32::ArchVersion::v7;
        config.always_little_endian = true;
        config.check_halt_on_memory_access = true;
        config.enable_cycle_counting = true;
        config.code_cache_size = 16U * 1024U * 1024U;
        return config;
    }

    Callbacks callbacks;
    Dynarmic::A32::Jit jit;
    std::uint64_t thread_id{};
    std::uint32_t fpexc{};
};

DynarmicCpu::DynarmicCpu(memory::MemoryBus& memory_bus)
    : impl_(std::make_unique<Impl>(memory_bus)) {}

DynarmicCpu::~DynarmicCpu() = default;

RunResult DynarmicCpu::Run(const std::uint64_t tick_budget) {
    const auto pc = memory::GuestAddress{impl_->jit.Regs()[15]};
    if (halt_requested_.exchange(false)) {
        impl_->jit.ClearHalt(kExternalHalt);
        return {0, RunStopReason::halt_requested, pc};
    }
    if (tick_budget == 0) return {0, RunStopReason::budget_exhausted, pc};

    impl_->callbacks.Begin(tick_budget, impl_->thread_id);
    impl_->jit.ClearHalt(kCallbackHalt | kExternalHalt);
    const auto halt_reason = impl_->jit.Run();
    const auto ticks = impl_->callbacks.TicksConsumed();
    impl_->jit.ClearHalt(halt_reason);

    if (impl_->callbacks.Pending().has_value()) {
        const auto& pending = *impl_->callbacks.Pending();
        return {ticks, pending.reason, pending.pc, pending.instruction,
                pending.immediate, pending.fault};
    }
    if (Dynarmic::Has(halt_reason, kExternalHalt) ||
        halt_requested_.exchange(false)) {
        return {ticks, RunStopReason::halt_requested,
                memory::GuestAddress{impl_->jit.Regs()[15]}};
    }
    return {ticks, RunStopReason::budget_exhausted,
            memory::GuestAddress{impl_->jit.Regs()[15]}};
}

A32State DynarmicCpu::GetState() const {
    A32State state;
    const auto& registers = impl_->jit.Regs();
    for (std::size_t index = 0; index < registers.size(); ++index) {
        state.SetRegister(static_cast<CoreRegister>(index), registers[index]);
    }
    const auto& extended = impl_->jit.ExtRegs();
    for (std::size_t index = 0; index < extended.size(); ++index) {
        state.SetExtendedRegister(static_cast<std::uint8_t>(index), extended[index]);
    }
    state.SetCpsr(impl_->jit.Cpsr());
    state.SetFpscr(impl_->jit.Fpscr());
    state.SetFpexc(impl_->fpexc);
    state.SetThreadId(impl_->thread_id);
    return state;
}

void DynarmicCpu::SetState(const A32State& state) {
    impl_->jit.Regs() = state.CoreRegisters();
    impl_->jit.ExtRegs() = state.ExtendedRegisters();
    impl_->jit.SetCpsr(state.Cpsr());
    impl_->jit.SetFpscr(state.Fpscr());
    impl_->fpexc = state.Fpexc();
    impl_->thread_id = state.ThreadId();
}

void DynarmicCpu::RequestHalt() noexcept {
    halt_requested_.store(true);
    impl_->jit.HaltExecution(kExternalHalt);
}

}  // namespace ogplay::cpu
