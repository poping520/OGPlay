#pragma once

// Internal interpreter state shared by the kernel translation units and
// intrinsic class files. Not installed; include order is private.

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/generated/opcode_table.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

namespace ogplay::runtime::dexvm {

namespace gen = ogplay::runtime::dexvm::generated;

struct Frame final {
    const LinkedMethod* method{};
    std::vector<Slot> regs;
    std::uint32_t pc{};
    std::uint32_t pending_advance{};  // caller pc advance after callee return
    VmValue last_result;
    VmObjectRef caught;  // consumed by move-exception
    VmObjectRef synchronized_monitor;
};

struct ThrowableState final {
    VmObjectRef message;
    VmObjectRef cause;
    std::string message_utf8;  // rendered lazily for diagnostics
    std::vector<VmStackEntry> stack;
};

struct InterpreterExecutionState final {
    // Deque keeps outer-frame references valid across nested pushes
    // (new-instance triggering <clinit>, native re-entry, ...).
    std::deque<Frame> frames;
    VmObjectRef pending_exception;
    DexClassId pending_exception_class;
    VmValue exit_result;
    std::uint64_t ticks{};
    std::uint64_t token{};
    // Depth of guest native frames this context currently has live on the
    // root guest stack (04 §1 outbound marshaling).
    std::uint32_t native_depth{};
    // Teardown handshake, read once per instruction by Tick().
    std::atomic<bool> stop_requested{false};
};

struct RawDexVmTraceEntry final {
    std::uint64_t sequence{};
    DexVmTraceKind kind{DexVmTraceKind::instruction};
    std::uint64_t context_token{};
    std::uint64_t tick{};
    DexClassId java_class;
    VmMethodId method;
    std::uint32_t dex_pc{};
    std::uint8_t opcode{};
    std::uint64_t value{};
};

class InterpreterExecutionScope final {
public:
    InterpreterExecutionScope(void* interpreter,
                              InterpreterExecutionState& execution);
    ~InterpreterExecutionScope();
    InterpreterExecutionScope(const InterpreterExecutionScope&) = delete;
    InterpreterExecutionScope& operator=(const InterpreterExecutionScope&) =
        delete;

private:
    void* interpreter_{};
    InterpreterExecutionState* previous_{};
};

class Interpreter::Impl final {
public:
    DexClassLinker* linker{};
    JavaObjectModel* model{};
    NativeMethodBridge* bridge{};
    core::CapabilityLedger* ledger{};
    InterpreterConfig config;
    InterpreterStats stats;

    // Guards the context table only. Execution() resolves the active state
    // from thread-local routing or the cached default, so the hot path never
    // touches the table and a teardown RequestStop from another host thread
    // stays safe.
    mutable std::mutex executions_mutex;
    std::unordered_map<std::uint64_t,
                       std::unique_ptr<InterpreterExecutionState>>
        executions;
    InterpreterExecutionState* default_execution{};
    std::uint64_t next_execution_token{2};

    std::unordered_map<std::uint32_t, ThrowableState> throwables;
    std::unordered_map<std::uint32_t, std::u16string> builders;
    std::unordered_map<std::uint32_t, std::vector<VmObjectRef>> lists;
    std::unordered_map<std::uint32_t,
                       std::vector<std::pair<VmObjectRef, VmObjectRef>>>
        maps;
    std::unordered_map<std::string, std::string> system_properties{
        {"file.separator", "/"},
        {"line.separator", "\n"},
        {"path.separator", ":"},
    };
    std::vector<IntrinsicStateTableHooks> intrinsic_state_tables;
    core::Logger* logger{};
    Interpreter* owner{};
    VmExecutionLock execution_lock;
    std::mutex clinit_wait_mutex;
    std::condition_variable clinit_changed;
    std::atomic<std::uint64_t> clinit_generation{};
    std::unique_ptr<VmMonitorTable> monitors;
    VmThreadRuntime* threads{};
    InterpreterGcIntegration gc_integration;
    std::vector<RawDexVmTraceEntry> trace_ring;
    std::size_t trace_head{};
    std::size_t trace_size{};
    std::uint64_t trace_sequence{};

    [[nodiscard]] InterpreterExecutionState& Execution();
    [[nodiscard]] const InterpreterExecutionState& Execution() const;
    [[nodiscard]] InterpreterExecutionState& Execution(
        const InterpreterExecutionContext& context);
    [[nodiscard]] const InterpreterExecutionState& Execution(
        const InterpreterExecutionContext& context) const;

    // ---- register access with tag enforcement (02 §7) -------------------

    [[noreturn]] void FailCode(const std::string& message) const {
        const auto& execution = Execution();
        const auto* frame = execution.frames.empty()
                                ? nullptr
                                : &execution.frames.back();
        std::string where = "dexvm";
        if (frame != nullptr) {
            where = linker->Class(frame->method->owner).descriptor + "." +
                    frame->method->name + " pc " +
                    std::to_string(frame->pc);
        }
        throw DexVmError(DexVmErrorReason::invalid_operand,
                         where + ": " + message);
    }

    [[nodiscard]] Slot& RegAt(Frame& frame, const std::uint32_t reg) {
        if (reg >= frame.regs.size()) {
            FailCode("register v" + std::to_string(reg) + " out of range");
        }
        return frame.regs[reg];
    }

    [[nodiscard]] std::uint32_t GetCat1(Frame& frame,
                                        const std::uint32_t reg) {
        const auto& slot = RegAt(frame, reg);
        if (slot.tag != SlotTag::cat1) {
            FailCode("register v" + std::to_string(reg) +
                     " does not hold a cat1 value");
        }
        return slot.bits;
    }
    [[nodiscard]] std::uint64_t GetWide(Frame& frame,
                                        const std::uint32_t reg) {
        const auto& lo = RegAt(frame, reg);
        const auto& hi = RegAt(frame, reg + 1);
        if (lo.tag != SlotTag::wide_lo || hi.tag != SlotTag::wide_hi) {
            FailCode("register pair v" + std::to_string(reg) +
                     " does not hold a complete wide value");
        }
        return static_cast<std::uint64_t>(lo.bits) |
               (static_cast<std::uint64_t>(hi.bits) << 32U);
    }
    [[nodiscard]] VmObjectRef GetRef(Frame& frame, const std::uint32_t reg) {
        auto& slot = RegAt(frame, reg);
        if (slot.tag == SlotTag::ref) return VmObjectRef(slot.bits);
        if (slot.tag == SlotTag::cat1 && slot.bits == 0) {
            // Dalvik zero-type relaxation: const 0 doubles as null.
            slot.tag = SlotTag::ref;
            return VmObjectRef{};
        }
        FailCode("register v" + std::to_string(reg) +
                 " does not hold a reference");
    }
    void SetCat1(Frame& frame, const std::uint32_t reg,
                 const std::uint32_t bits) {
        auto& slot = RegAt(frame, reg);
        slot.bits = bits;
        slot.tag = SlotTag::cat1;
    }
    void SetWide(Frame& frame, const std::uint32_t reg,
                 const std::uint64_t bits) {
        auto& lo = RegAt(frame, reg);
        auto& hi = RegAt(frame, reg + 1);
        lo.bits = static_cast<std::uint32_t>(bits);
        lo.tag = SlotTag::wide_lo;
        hi.bits = static_cast<std::uint32_t>(bits >> 32U);
        hi.tag = SlotTag::wide_hi;
    }
    void SetRef(Frame& frame, const std::uint32_t reg,
                const VmObjectRef ref) {
        auto& slot = RegAt(frame, reg);
        slot.bits = ref.Value();
        slot.tag = SlotTag::ref;
    }

    // ---- exceptions -------------------------------------------------------

    void ThrowJava(const std::string& descriptor, const std::string& message);
    void SetPending(VmObjectRef throwable);
    [[nodiscard]] std::vector<VmStackEntry> CaptureStack() const;
    void RecordTrace(DexVmTraceKind kind,
                     const InterpreterExecutionState& execution,
                     const LinkedMethod* method = nullptr,
                     std::uint32_t dex_pc = 0,
                     std::uint8_t opcode = 0,
                     std::uint64_t value = 0);

    // ---- execution --------------------------------------------------------

    // The hot path resolves the active execution once per Call entry and
    // threads the reference through Run/Step/Tick instead of re-resolving
    // the thread-local routing on every instruction.
    void Tick(InterpreterExecutionState& execution,
              const std::uint64_t amount = 1) {
        execution.ticks += amount;
        if (execution.stop_requested.load(std::memory_order_relaxed)) {
            throw DexVmError(DexVmErrorReason::thread_stopped,
                             "dexvm thread stopped at teardown after " +
                                 std::to_string(execution.ticks) + " ticks");
        }
        if (execution.ticks > config.tick_budget) {
            throw DexVmError(DexVmErrorReason::budget_exhausted,
                             "dexvm tick budget exhausted after " +
                                 std::to_string(execution.ticks) + " ticks");
        }
    }

    // Marks live guest native frames for teardown integrity checks.
    class NativeFrame final {
    public:
        explicit NativeFrame(Impl& impl) : execution_(&impl.Execution()) {
            ++execution_->native_depth;
        }
        ~NativeFrame() { --execution_->native_depth; }
        NativeFrame(const NativeFrame&) = delete;
        NativeFrame& operator=(const NativeFrame&) = delete;

    private:
        InterpreterExecutionState* execution_;
    };

    // Runs frames until depth drops below entry_depth; returns outcome.
    [[nodiscard]] VmCallOutcome Run(InterpreterExecutionState& execution,
                                    std::size_t entry_depth);

    // Executes one instruction of the top frame. Returns true when the
    // frame stack changed (push/pop) or pc was redirected.
    void Step(InterpreterExecutionState& execution);

    // Family handlers (separate translation units).
    [[nodiscard]] bool ExecuteArithmetic(Frame& frame, std::uint8_t opcode,
                                         std::uint16_t unit);
    void StepObjectOrInvoke(InterpreterExecutionState& execution, Frame& frame,
                            std::uint8_t opcode, std::uint16_t unit);

    // Invocation plumbing.
    void PushInterpretedFrame(InterpreterExecutionState& execution,
                              const LinkedMethod& method,
                              std::span<const VmValue> arguments,
                              std::uint32_t caller_advance);
    [[nodiscard]] VmObjectRef MethodMonitor(
        const LinkedMethod& method,
        std::span<const VmValue> arguments) const;
    void ReleaseFrameMonitor(Frame& frame) noexcept;
    void PublishClinitState(DexClassId java_class, ClinitState state);

    class MethodMonitorScope final {
    public:
        MethodMonitorScope(Impl& impl, const LinkedMethod& method,
                           std::span<const VmValue> arguments)
            : impl_(&impl), method_(&method),
              object_(impl.MethodMonitor(method, arguments)) {
            if (object_.IsValid()) {
                impl_->monitors->Enter(object_, impl_->Execution().token);
                impl_->RecordTrace(DexVmTraceKind::monitor_enter,
                                   impl_->Execution(), &method, 0, 0,
                                   object_.Value());
            }
        }
        ~MethodMonitorScope() {
            if (!object_.IsValid()) return;
            try {
                impl_->monitors->Exit(object_, impl_->Execution().token);
                impl_->RecordTrace(DexVmTraceKind::monitor_exit,
                                   impl_->Execution(), method_, 0, 0,
                                   object_.Value());
            } catch (const std::exception&) {
            }
        }
        MethodMonitorScope(const MethodMonitorScope&) = delete;
        MethodMonitorScope& operator=(const MethodMonitorScope&) = delete;

    private:
        Impl* impl_{};
        const LinkedMethod* method_{};
        VmObjectRef object_;
    };
    [[nodiscard]] VmValue InvokeIntrinsic(const LinkedMethod& method,
                                          VmObjectRef receiver,
                                          std::span<const VmValue> arguments);
    void EnsureInitialized(InterpreterExecutionState& execution,
                           DexClassId java_class);

    [[nodiscard]] VmObjectRef AllocateInstance(DexClassId java_class);

    // Utility: convert modified-UTF8-ish ASCII to UTF-16 and back.
    [[nodiscard]] VmObjectRef InternDexString(std::uint32_t string_index);
    void TraceIntrinsicSideTables(VmObjectRef owner,
                                  const VmRootVisitor& visitor) const;
    void PrepareSafeAllocation(std::uint64_t request_bytes,
                               std::string_view trigger);
};

}  // namespace ogplay::runtime::dexvm
