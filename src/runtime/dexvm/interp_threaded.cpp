// FastCode threaded interpreter. The steady-state loop lives in one function:
// GCC/Clang dispatch with per-opcode labels-as-values, MSVC with a dense
// handler switch. Family bodies are included as fragments so handler tails
// fetch the next FastCode index without returning to Run() or calling
// frames.back() on the same-frame path.

#include "interpreter_internal.h"

#include <algorithm>
#include <array>
#include <span>

namespace ogplay::runtime::dexvm {
namespace {

constexpr std::uint32_t kObjectElementFlag = 0x80000000U;

[[nodiscard]] JniPrimitiveKind ArrayKindFor(const std::string& element) {
    if (element == "Z") return JniPrimitiveKind::boolean;
    if (element == "B") return JniPrimitiveKind::byte;
    if (element == "C") return JniPrimitiveKind::character;
    if (element == "S") return JniPrimitiveKind::short_integer;
    if (element == "I") return JniPrimitiveKind::integer;
    if (element == "J") return JniPrimitiveKind::long_integer;
    if (element == "F") return JniPrimitiveKind::float_value;
    if (element == "D") return JniPrimitiveKind::double_value;
    throw DexVmError(DexVmErrorReason::invalid_operand,
                     "not a primitive array element: " + element);
}

}  // namespace

void Interpreter::Impl::StepThreaded(
    InterpreterExecutionState& execution) {
    auto& frames = execution.frames;
    auto& pending_exception = execution.pending_exception;
    auto& exit_result = execution.exit_result;
    if (frames.empty()) return;

    if (config.force_all_bridge) {
        const auto depth = frames.size();
        while (frames.size() == depth && !pending_exception.IsValid()) {
            Step(execution);
        }
        return;
    }

    Frame* frame_ptr = &frames.back();
    Slot* regs = frame_ptr->regs.data();
    const bool needs_build = !frame_ptr->method->fast_code;
    if (needs_build) {
        static_cast<void>(linker->FastCodeFor(frame_ptr->method->id));
        ++stats.fast_code_builds;
        stats.fast_code_bytes += frame_ptr->method->fast_code->storage_bytes;
        frame_ptr = &frames.back();
        regs = frame_ptr->regs.data();
    }
    const FastCode* code_ptr = frame_ptr->method->fast_code.get();
    std::uint32_t ip = code_ptr->IndexForDexPc(frame_ptr->pc);
    const auto entry_depth = frames.size();
    std::uint64_t ticks = execution.ticks;
    std::uint64_t executed = 0;
    const FastInstruction* insn = nullptr;

    const auto add_ticks = [&](const std::uint64_t amount) {
        ticks += amount;
        execution.ticks = ticks;
        if (execution.stop_requested.load(std::memory_order_relaxed)) {
            stats.executed_instructions += executed;
            throw DexVmError(DexVmErrorReason::thread_stopped,
                             "dexvm thread stopped at teardown after " +
                                 std::to_string(ticks) + " ticks");
        }
        if (ticks > config.tick_budget) {
            stats.executed_instructions += executed;
            throw DexVmError(DexVmErrorReason::budget_exhausted,
                             "dexvm tick budget exhausted after " +
                                 std::to_string(ticks) + " ticks");
        }
    };

#include "interp_threaded_op_labels.inc"

    goto fetch;

reload:
    if (pending_exception.IsValid() || frames.size() != entry_depth) {
        goto yield;
    }
    frame_ptr = &frames.back();
    regs = frame_ptr->regs.data();
    code_ptr = frame_ptr->method->fast_code.get();
    ip = code_ptr->IndexForDexPc(frame_ptr->pc);

fetch:
    if (pending_exception.IsValid() || frames.size() != entry_depth) {
        goto yield;
    }
    insn = &code_ptr->instructions[ip];
    frame_ptr->pc = insn->dex_pc;

    if (insn->handler == FastHandler::bridge) {
        goto bridge;
    }

    add_ticks(1);
    ++executed;
    if (!trace_ring.empty()) {
        RecordTrace(DexVmTraceKind::instruction, execution, frame_ptr->method,
                    insn->dex_pc, insn->opcode);
    }

#if defined(__GNUC__) && !defined(_MSC_VER)
    goto* op_labels[insn->opcode];
#else
    switch (insn->handler) {
        case FastHandler::straight:
            goto straight;
        case FastHandler::object_checked:
        case FastHandler::object_fast:
            goto object;
        case FastHandler::invoke_checked:
        case FastHandler::invoke_fast:
            goto invoke;
        case FastHandler::bridge:
            goto bridge;
        default:
            FailCode("unknown FastHandler in threaded dispatch");
    }
#endif

straight: {
    Frame& frame = *frame_ptr;
    const FastInstruction& instruction = *insn;
    const auto opcode = instruction.opcode;
#include "interp_threaded_straight.inc"
}

object: {
    Frame& frame = *frame_ptr;
    const FastCode& code = *code_ptr;
    const FastInstruction& instruction = *insn;
    const auto opcode = instruction.opcode;
#include "interp_threaded_object.inc"
}

invoke: {
    Frame& frame = *frame_ptr;
    const FastCode& code = *code_ptr;
    const FastInstruction& instruction = *insn;
#include "interp_threaded_invoke.inc"
}

bridge:
    stats.executed_instructions += executed;
    executed = 0;
    execution.ticks = ticks;
    Step(execution);
    ticks = execution.ticks;
    goto reload;

yield:
    stats.executed_instructions += executed;
    execution.ticks = ticks;
    return;

#include "interp_threaded_op_stubs.inc"
}

}  // namespace ogplay::runtime::dexvm
