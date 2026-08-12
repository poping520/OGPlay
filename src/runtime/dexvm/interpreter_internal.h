#pragma once

// Internal interpreter state shared by the kernel translation units
// (interpreter.cpp, interp_exec.cpp, interp_arith.cpp,
// interpreter_builtins.cpp). Not installed; include order is private.

#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/generated/opcode_table.h"

namespace ogplay::runtime::dexvm {

namespace gen = ogplay::runtime::dexvm::generated;

struct Frame final {
    const LinkedMethod* method{};
    std::vector<Slot> regs;
    std::uint32_t pc{};
    std::uint32_t pending_advance{};  // caller pc advance after callee return
    VmValue last_result;
    VmObjectRef caught;  // consumed by move-exception
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
    std::unordered_map<std::uint32_t, std::int32_t> monitors;
    std::uint64_t ticks{};
    std::uint64_t token{};
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
    IntrinsicRegistry intrinsics;
    NativeMethodBridge* bridge{};
    core::CapabilityLedger* ledger{};
    InterpreterConfig config;
    InterpreterStats stats;

    std::unordered_map<std::uint64_t,
                       std::unique_ptr<InterpreterExecutionState>>
        executions;
    std::uint64_t next_execution_token{2};

    std::unordered_map<std::uint32_t, ThrowableState> throwables;
    std::unordered_map<std::uint32_t, std::u16string> builders;
    std::unordered_map<std::uint32_t, std::vector<VmObjectRef>> lists;
    std::unordered_map<std::uint32_t,
                       std::vector<std::pair<VmObjectRef, VmObjectRef>>>
        maps;
    core::Logger* logger{};
    Interpreter* owner{};

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

    // ---- execution --------------------------------------------------------

    void Tick(const std::uint64_t amount = 1) {
        auto& execution = Execution();
        execution.ticks += amount;
        if (execution.ticks > config.tick_budget) {
            throw DexVmError(DexVmErrorReason::budget_exhausted,
                             "dexvm tick budget exhausted after " +
                                 std::to_string(execution.ticks) + " ticks");
        }
    }

    // Runs frames until depth drops below entry_depth; returns outcome.
    [[nodiscard]] VmCallOutcome Run(std::size_t entry_depth);

    // Executes one instruction of the top frame. Returns true when the
    // frame stack changed (push/pop) or pc was redirected.
    void Step();

    // Family handlers (separate translation units).
    [[nodiscard]] bool ExecuteArithmetic(Frame& frame, std::uint8_t opcode,
                                         std::uint16_t unit);
    void StepObjectOrInvoke(Frame& frame, std::uint8_t opcode,
                            std::uint16_t unit);

    // Invocation plumbing.
    void PushInterpretedFrame(const LinkedMethod& method,
                              std::span<const VmValue> arguments,
                              std::uint32_t caller_advance);
    [[nodiscard]] VmValue InvokeIntrinsic(const LinkedMethod& method,
                                          VmObjectRef receiver,
                                          std::span<const VmValue> arguments);
    void EnsureInitialized(DexClassId java_class);

    [[nodiscard]] VmObjectRef AllocateInstance(DexClassId java_class);

    // Utility: convert modified-UTF8-ish ASCII to UTF-16 and back.
    [[nodiscard]] VmObjectRef InternDexString(std::uint32_t string_index);
};

// Registers built-in java.* core handlers into a registry.
void RegisterCoreBuiltinHandlers(IntrinsicRegistry& registry);
// P1 batch: StringBuilder/System/Math/boxed values/collections/streams.
void RegisterJavaCoreBuiltins(IntrinsicRegistry& registry);
// String/StringBuilder surface (intrinsics_string.cpp); called by
// RegisterJavaCoreBuiltins.
void RegisterJavaStringBuiltins(IntrinsicRegistry& registry);

}  // namespace ogplay::runtime::dexvm
