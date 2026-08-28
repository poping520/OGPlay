// FastCode threaded interpreter. The steady-state loop lives in one function:
// GCC/Clang dispatch with per-opcode labels-as-values and an indirect goto at
// every handler tail; MSVC with a dense handler switch loop. Family bodies are
// included as fragments. Same-frame tails never return to Run() or call
// frames.back(). ticks/executed stay local until a sync point.

#include "interpreter_internal.h"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace ogplay::runtime::dexvm {
namespace {

constexpr std::uint32_t kObjectElementFlag = 0x80000000U;

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
    const auto entry_depth = frames.size();
    std::uint64_t ticks = execution.ticks;
    std::uint64_t executed = 0;
    const FastInstruction* insn = nullptr;
    std::uint32_t ip = kInvalidFastIndex;

    const auto resolve_ip = [&]() {
        if (frame_ptr->fast_ip != kInvalidFastIndex) {
            ip = frame_ptr->fast_ip;
            frame_ptr->fast_ip = kInvalidFastIndex;
        } else {
            ip = code_ptr->IndexForDexPc(frame_ptr->pc);
        }
    };

    const auto consume_extra_ticks = [&](const std::uint64_t amount) {
        if (amount == 0U) return;
        ticks += amount;
        if (execution.stop_requested.load(std::memory_order_relaxed)) {
            stats.executed_instructions += executed;
            executed = 0;
            execution.ticks = ticks;
            throw DexVmError(DexVmErrorReason::thread_stopped,
                             "dexvm thread stopped at teardown after " +
                                 std::to_string(ticks) + " ticks");
        }
        if (ticks > config.tick_budget) {
            stats.executed_instructions += executed;
            executed = 0;
            execution.ticks = ticks;
            throw DexVmError(DexVmErrorReason::budget_exhausted,
                             "dexvm tick budget exhausted after " +
                                 std::to_string(ticks) + " ticks");
        }
    };

#define OGPLAY_SYNC_EXECUTION()                                 \
    do {                                                        \
        stats.executed_instructions += executed;                \
        executed = 0;                                           \
        execution.ticks = ticks;                                \
    } while (0)

#define OGPLAY_RECORD_TRACE(...)                                \
    do {                                                        \
        if (!trace_ring.empty()) {                              \
            execution.ticks = ticks;                            \
            RecordTrace(__VA_ARGS__);                           \
        }                                                       \
    } while (0)

#define OGPLAY_FETCH_AND_TICK()                                 \
    do {                                                        \
        insn = &code_ptr->instructions[ip];                     \
        frame_ptr->pc = insn->dex_pc;                           \
        if (insn->handler == FastHandler::bridge) goto bridge;  \
        ticks += 1U;                                            \
        ++executed;                                             \
        if (execution.stop_requested.load(                      \
                std::memory_order_relaxed)) {                   \
            OGPLAY_SYNC_EXECUTION();                            \
            throw DexVmError(                                   \
                DexVmErrorReason::thread_stopped,               \
                "dexvm thread stopped at teardown after " +     \
                    std::to_string(ticks) + " ticks");          \
        }                                                       \
        if (ticks > config.tick_budget) {                       \
            OGPLAY_SYNC_EXECUTION();                            \
            throw DexVmError(                                   \
                DexVmErrorReason::budget_exhausted,             \
                "dexvm tick budget exhausted after " +          \
                    std::to_string(ticks) + " ticks");          \
        }                                                       \
        OGPLAY_RECORD_TRACE(DexVmTraceKind::instruction,        \
                            execution, frame_ptr->method,       \
                            insn->dex_pc, insn->opcode);        \
    } while (0)

#if defined(__GNUC__) && !defined(_MSC_VER)
#define OGPLAY_DISPATCH_AT()                                    \
    do {                                                        \
        OGPLAY_FETCH_AND_TICK();                                \
        goto* op_labels[insn->opcode];                          \
    } while (0)
#define OGPLAY_DISPATCH_NEXT()                                  \
    do {                                                        \
        ip += 1U;                                               \
        OGPLAY_DISPATCH_AT();                                   \
    } while (0)
#define OGPLAY_DISPATCH_TO(idx)                                 \
    do {                                                        \
        ip = (idx);                                             \
        OGPLAY_DISPATCH_AT();                                   \
    } while (0)
#else
#define OGPLAY_DISPATCH_AT() goto fetch_at
#define OGPLAY_DISPATCH_NEXT()                                  \
    do {                                                        \
        ip += 1U;                                               \
        goto fetch_at;                                          \
    } while (0)
#define OGPLAY_DISPATCH_TO(idx)                                 \
    do {                                                        \
        ip = (idx);                                             \
        goto fetch_at;                                          \
    } while (0)
#endif

#include "interp_threaded_op_labels.inc"

    goto reload;

reload:
    if (pending_exception.IsValid() || frames.size() != entry_depth) {
        goto yield;
    }
    frame_ptr = &frames.back();
    regs = frame_ptr->regs.data();
    code_ptr = frame_ptr->method->fast_code.get();
    resolve_ip();
    OGPLAY_DISPATCH_AT();

#if !defined(__GNUC__) || defined(_MSC_VER)
fetch_at:
    OGPLAY_FETCH_AND_TICK();
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
    OGPLAY_SYNC_EXECUTION();
    Step(execution);
    ticks = execution.ticks;
    goto reload;

yield:
    OGPLAY_SYNC_EXECUTION();
    return;

#include "interp_threaded_op_stubs.inc"

#undef OGPLAY_DISPATCH_TO
#undef OGPLAY_DISPATCH_NEXT
#undef OGPLAY_DISPATCH_AT
#undef OGPLAY_FETCH_AND_TICK
#undef OGPLAY_RECORD_TRACE
#undef OGPLAY_SYNC_EXECUTION
}

}  // namespace ogplay::runtime::dexvm
