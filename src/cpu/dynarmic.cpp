#include "ogplay/cpu/dynarmic.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/coprocessor.h>
#include <dynarmic/interface/exclusive_monitor.h>

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

class ThreadPointerCoprocessor final : public Dynarmic::A32::Coprocessor {
public:
    explicit ThreadPointerCoprocessor(std::uint32_t& value) noexcept
        : value_(value) {}

    std::optional<Callback> CompileInternalOperation(
        bool, unsigned, Dynarmic::A32::CoprocReg, Dynarmic::A32::CoprocReg,
        Dynarmic::A32::CoprocReg, unsigned) override {
        return std::nullopt;
    }
    CallbackOrAccessOneWord CompileSendOneWord(
        bool, unsigned, Dynarmic::A32::CoprocReg, Dynarmic::A32::CoprocReg,
        unsigned) override {
        return std::monostate{};
    }
    CallbackOrAccessTwoWords CompileSendTwoWords(
        bool, unsigned, Dynarmic::A32::CoprocReg) override {
        return std::monostate{};
    }
    CallbackOrAccessOneWord CompileGetOneWord(
        const bool two, const unsigned opc1, const Dynarmic::A32::CoprocReg crn,
        const Dynarmic::A32::CoprocReg crm, const unsigned opc2) override {
        if (!two && opc1 == 0 && crn == Dynarmic::A32::CoprocReg::C13 &&
            crm == Dynarmic::A32::CoprocReg::C0 && opc2 == 3) {
            return &value_;
        }
        return std::monostate{};
    }
    CallbackOrAccessTwoWords CompileGetTwoWords(
        bool, unsigned, Dynarmic::A32::CoprocReg) override {
        return std::monostate{};
    }
    std::optional<Callback> CompileLoadWords(
        bool, bool, Dynarmic::A32::CoprocReg,
        std::optional<std::uint8_t>) override {
        return std::nullopt;
    }
    std::optional<Callback> CompileStoreWords(
        bool, bool, Dynarmic::A32::CoprocReg,
        std::optional<std::uint8_t>) override {
        return std::nullopt;
    }

private:
    std::uint32_t& value_;
};

}  // namespace

class DynarmicExecutionContext::Impl final {
public:
    explicit Impl(const std::size_t maximum_processors)
        : monitor(maximum_processors), processors(maximum_processors) {
        if (maximum_processors == 0) {
            throw std::invalid_argument(
                "Dynarmic execution context requires a processor");
        }
    }

    Dynarmic::ExclusiveMonitor monitor;
    std::mutex mutex;
    std::mutex memory_mutex;
    std::vector<bool> processors;
};

DynarmicExecutionContext::DynarmicExecutionContext(
    const std::size_t maximum_processors)
    : impl_(std::make_unique<Impl>(maximum_processors)) {}

DynarmicExecutionContext::~DynarmicExecutionContext() = default;

std::size_t DynarmicExecutionContext::AcquireProcessor() {
    std::scoped_lock lock(impl_->mutex);
    for (std::size_t index = 0; index < impl_->processors.size(); ++index) {
        if (!impl_->processors[index]) {
            impl_->processors[index] = true;
            return index;
        }
    }
    throw std::runtime_error("Dynarmic execution context is full");
}

void DynarmicExecutionContext::ReleaseProcessor(
    const std::size_t processor_id) noexcept {
    std::scoped_lock lock(impl_->mutex);
    if (processor_id < impl_->processors.size()) {
        impl_->processors[processor_id] = false;
        impl_->monitor.ClearProcessor(processor_id);
    }
}

class DynarmicCpu::Impl final {
public:
    class Callbacks final : public Dynarmic::A32::UserCallbacks {
    public:
        Callbacks(memory::MemoryBus& memory_bus,
                  std::mutex& memory_mutex) noexcept
            : memory_bus_(memory_bus), memory_mutex_(memory_mutex) {}

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

        [[nodiscard]] memory::DirectMemoryPageTable* DirectPageTable() noexcept {
            return memory_bus_.DirectPageTable();
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

        bool MemoryWriteExclusive8(const Dynarmic::A32::VAddr address,
                                   const std::uint8_t value,
                                   const std::uint8_t expected) override {
            return WriteExclusive(address, value, expected,
                                  &memory::MemoryBus::Read8,
                                  &memory::MemoryBus::Write8);
        }
        bool MemoryWriteExclusive16(const Dynarmic::A32::VAddr address,
                                    const std::uint16_t value,
                                    const std::uint16_t expected) override {
            return WriteExclusive(address, value, expected,
                                  &memory::MemoryBus::Read16,
                                  &memory::MemoryBus::Write16);
        }
        bool MemoryWriteExclusive32(const Dynarmic::A32::VAddr address,
                                    const std::uint32_t value,
                                    const std::uint32_t expected) override {
            return WriteExclusive(address, value, expected,
                                  &memory::MemoryBus::Read32,
                                  &memory::MemoryBus::Write32);
        }
        bool MemoryWriteExclusive64(const Dynarmic::A32::VAddr address,
                                    const std::uint64_t value,
                                    const std::uint64_t expected) override {
            return WriteExclusive(address, value, expected,
                                  &memory::MemoryBus::Read64,
                                  &memory::MemoryBus::Write64);
        }

        void InterpreterFallback(const Dynarmic::A32::VAddr pc,
                                 std::size_t) override {
            RecordStop({RunStopReason::undefined_instruction,
                        memory::GuestAddress{pc}, 0, 0, std::nullopt});
        }

        void CallSVC(const std::uint32_t immediate) override {
            const bool thumb = (jit_->Cpsr() & kThumbBit) != 0;
            const auto instruction_size = thumb ? 2U : 4U;
            const auto next_pc = jit_->Regs()[15];
            const auto instruction = thumb ? 0xdf00U | (immediate & 0xffU)
                                           : 0xef000000U | (immediate & 0x00ffffffU);
            RecordStop({RunStopReason::supervisor_call,
                        memory::GuestAddress{next_pc - instruction_size},
                        instruction, immediate, std::nullopt});
        }

        void ExceptionRaised(const Dynarmic::A32::VAddr pc,
                             const Dynarmic::A32::Exception exception) override {
            const auto reason = exception == Dynarmic::A32::Exception::Breakpoint
                                    ? RunStopReason::breakpoint
                                    : RunStopReason::undefined_instruction;
            RecordStop(
                {reason, memory::GuestAddress{pc}, 0, 0, std::nullopt});
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
                std::scoped_lock lock(memory_mutex_);
                (memory_bus_.*function)(memory::GuestAddress{address}, value, thread_id_);
            } catch (const memory::MemoryFault& fault) {
                RecordFault(fault);
            }
        }

        template <typename UInt>
        [[nodiscard]] bool WriteExclusive(
            const Dynarmic::A32::VAddr address, const UInt value,
            const UInt expected, const ReadFunction<UInt> read,
            const WriteFunction<UInt> write) {
            try {
                std::scoped_lock lock(memory_mutex_);
                const auto guest_address = memory::GuestAddress{address};
                if ((memory_bus_.*read)(guest_address, thread_id_) != expected) {
                    return false;
                }
                (memory_bus_.*write)(guest_address, value, thread_id_);
                return true;
            } catch (const memory::MemoryFault& fault) {
                RecordFault(fault);
                return false;
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
        std::mutex& memory_mutex_;
        Dynarmic::A32::Jit* jit_{};
        std::uint64_t thread_id_{};
        std::uint64_t ticks_remaining_{};
        std::uint64_t ticks_consumed_{};
        std::optional<PendingStop> pending_;
    };

    Impl(memory::MemoryBus& memory_bus,
         std::shared_ptr<DynarmicExecutionContext> execution_context)
        : context(std::move(execution_context)),
          processor_id(context->AcquireProcessor()),
          callbacks(memory_bus, context->impl_->memory_mutex),
          jit(MakeConfig(callbacks, thread_pointer, *context, processor_id)) {
        callbacks.Attach(jit);
    }

    ~Impl() { context->ReleaseProcessor(processor_id); }

    static Dynarmic::A32::UserConfig MakeConfig(
        Callbacks& callbacks, std::uint32_t& thread_pointer,
        DynarmicExecutionContext& context, const std::size_t processor_id) {
        Dynarmic::A32::UserConfig config{&callbacks};
        config.processor_id = processor_id;
        config.global_monitor = &context.impl_->monitor;
        config.arch_version = Dynarmic::A32::ArchVersion::v7;
        config.always_little_endian = true;
        config.page_table = callbacks.DirectPageTable();
        config.detect_misaligned_access_via_page_table =
            8U | 16U | 32U | 64U;
        config.only_detect_misalignment_via_page_table_on_page_boundary = true;
        // OGPlay uses callback-only memory access. Dynarmic's per-access abort
        // checks apply to page-table/fastmem fallbacks, while enabling this flag
        // still disables register get/set elimination on the Arm64 backend.
        config.check_halt_on_memory_access = false;
        config.enable_cycle_counting = true;
        // 16 MiB was exhausted by real titles at steady state: Dynarmic clears
        // the whole cache when full and recompiles hot blocks every frame.
        // Dynarmic caps the cache at 128 MiB; the mapping is committed lazily.
        config.code_cache_size = 64U * 1024U * 1024U;
        config.coprocessors[15] =
            std::make_shared<ThreadPointerCoprocessor>(thread_pointer);
        return config;
    }

    std::shared_ptr<DynarmicExecutionContext> context;
    std::size_t processor_id{};
    Callbacks callbacks;
    std::uint32_t thread_pointer{};
    Dynarmic::A32::Jit jit;
    std::uint64_t thread_id{};
    std::uint32_t fpexc{};
};

DynarmicCpu::DynarmicCpu(memory::MemoryBus& memory_bus)
    : DynarmicCpu(memory_bus,
                  std::make_shared<DynarmicExecutionContext>(1)) {}

DynarmicCpu::DynarmicCpu(
    memory::MemoryBus& memory_bus,
    std::shared_ptr<DynarmicExecutionContext> context)
    : impl_(nullptr) {
    if (!context) {
        throw std::invalid_argument("Dynarmic execution context is null");
    }
    impl_ = std::make_unique<Impl>(memory_bus, std::move(context));
}

DynarmicCpu::~DynarmicCpu() = default;

RunResult DynarmicCpu::Run(const std::uint64_t tick_budget) {
    const auto pc = memory::GuestAddress{impl_->jit.Regs()[15]};
    if (halt_requested_.exchange(false)) {
        impl_->jit.ClearHalt(kExternalHalt);
        return {0, RunStopReason::halt_requested, pc, 0, 0, std::nullopt};
    }
    if (tick_budget == 0) {
        return {0, RunStopReason::budget_exhausted, pc, 0, 0, std::nullopt};
    }

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
                memory::GuestAddress{impl_->jit.Regs()[15]}, 0, 0,
                std::nullopt};
    }
    return {ticks, RunStopReason::budget_exhausted,
            memory::GuestAddress{impl_->jit.Regs()[15]}, 0, 0,
            std::nullopt};
}

A32State DynarmicCpu::GetState() const {
    A32State state;
    state.SetCoreRegisters(impl_->jit.Regs());
    state.SetExtendedRegisters(impl_->jit.ExtRegs());
    state.SetCpsr(impl_->jit.Cpsr());
    state.SetFpscr(impl_->jit.Fpscr());
    state.SetFpexc(impl_->fpexc);
    state.SetThreadId(impl_->thread_id);
    state.SetThreadPointer(memory::GuestAddress{impl_->thread_pointer});
    return state;
}

void DynarmicCpu::SetState(const A32State& state) {
    impl_->jit.Regs() = state.CoreRegisters();
    impl_->jit.ExtRegs() = state.ExtendedRegisters();
    impl_->jit.SetCpsr(state.Cpsr());
    impl_->jit.SetFpscr(state.Fpscr());
    impl_->fpexc = state.Fpexc();
    impl_->thread_id = state.ThreadId();
    impl_->thread_pointer = state.ThreadPointer().Value();
}

void DynarmicCpu::RequestHalt() noexcept {
    halt_requested_.store(true);
    impl_->jit.HaltExecution(kExternalHalt);
}

}  // namespace ogplay::cpu
