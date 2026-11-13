#include "ogplay/runtime/dexvm/class_name_codec.h"
// FastCode threaded interpreter. The steady-state loop lives in one function:
// GCC/Clang dispatch with per-opcode labels-as-values and an indirect goto at
// every handler tail; MSVC with a dense handler switch loop. Family bodies are
// included as fragments. Same-frame tails never return to Run() or call
// frames.back(). ticks/executed stay local until a sync point.

#include "interpreter_internal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <optional>
#include <span>
#include <vector>

namespace ogplay::runtime::dexvm {
namespace {

constexpr std::uint32_t kObjectElementFlag = 0x80000000U;

std::int32_t FindStringCodePoint(std::u16string_view text, std::int32_t ch, std::int32_t start, bool reverse) {
    if (ch < 0 || ch > 0x10ffff || (reverse && start < 0)) return -1;
    const auto from = static_cast<std::size_t>(std::max(start, 0));
    if (ch <= 0xffff) {
        if (!reverse) return FastIndexOfStringChar(text, ch, start);
        const auto pos = text.rfind(static_cast<char16_t>(ch), from);
        return pos == text.npos ? -1 : static_cast<std::int32_t>(pos);
    }
    const auto cp = ch - 0x10000;
    const char16_t pair[]{static_cast<char16_t>(0xd800 + (cp >> 10)), static_cast<char16_t>(0xdc00 + (cp & 0x3ff))};
    const std::u16string_view needle{pair, 2};
    const auto pos = reverse ? text.rfind(needle, from) : text.find(needle, from);
    return pos == text.npos ? -1 : static_cast<std::int32_t>(pos);
}

bool TryStringFastOp(StringFastOp op, JavaObjectModel& model, const FastInvoke& invoke, const Slot* regs,
                     VmValue& result) {
    using Op = StringFastOp;
    const auto slot = [&](std::size_t i) -> const Slot& { return regs[invoke.registers[i]]; };
    const auto num = [&](std::size_t i) { return std::bit_cast<std::int32_t>(slot(i).bits); };
    const auto ref = [&](std::size_t i) { return VmObjectRef{slot(i).bits}; };
    if (slot(0).tag != SlotTag::ref || !model.IsString(ref(0))) return false;
    // Integer zero can also represent null in a DEX argument register.
    const auto ref_slot = [&](std::size_t i) {
        return slot(i).tag == SlotTag::ref || (slot(i).tag == SlotTag::cat1 && slot(i).bits == 0);
    };
    for (std::size_t i = 0; i < invoke.argument_shorty.size(); ++i) {
        if (invoke.argument_shorty[i] == 'L' ? !ref_slot(i + 1) : slot(i + 1).tag != SlotTag::cat1) return false;
    }
    const auto self = ref(0);
    const auto write = [&](std::int32_t value) {
        result = VmValue::Int(value);
        return true;
    };
    switch (op) {
    case Op::length: return write(model.StringLength(self));
    case Op::is_empty: return write(model.StringLength(self) == 0);
    case Op::hash_code: return write(model.StringHashCode(self));
    case Op::equals: return write(model.StringEquals(self, ref(1)));
    case Op::char_at:
        if (num(1) < 0 || num(1) >= model.StringLength(self)) return false;
        return write(model.StringChars(self)[static_cast<std::size_t>(num(1))]);
    default: break;
    }

    const auto text = model.StringChars(self);
    const auto size = static_cast<std::int32_t>(text.size());
    switch (op) {
    case Op::fast_index_of: return write(FastIndexOfStringChar(text, num(1), num(2)));
    case Op::index_char: return write(FindStringCodePoint(text, num(1), 0, false));
    case Op::index_char_from: return write(FindStringCodePoint(text, num(1), num(2), false));
    case Op::last_char: return write(FindStringCodePoint(text, num(1), size, true));
    case Op::last_char_from: return write(FindStringCodePoint(text, num(1), num(2), true));
    default: break;
    }

    const auto other_index = op == Op::region_matches_case ? 3U : (op == Op::region_matches ? 2U : 1U);
    const auto other = ref(other_index);
    if (!model.IsString(other)) return false;
    const auto needle = model.StringChars(other);
    const auto needle_size = static_cast<std::int32_t>(needle.size());
    switch (op) {
    case Op::compare_to: return write(CompareStringChars(text, needle));
    case Op::content_equals: return write(text == needle);
    case Op::index_string:
    case Op::index_string_from: {
        const auto start = op == Op::index_string ? 0 : std::max(num(2), 0);
        if (needle.empty()) return write(std::min(start, size));
        const auto pos = text.find(needle, static_cast<std::size_t>(start));
        return write(pos == text.npos ? -1 : static_cast<std::int32_t>(pos));
    }
    case Op::last_string:
    case Op::last_string_from: {
        const auto start = op == Op::last_string ? size : num(2);
        if (start < 0) return write(-1);
        const auto pos = text.rfind(needle, static_cast<std::size_t>(start));
        return write(pos == text.npos ? -1 : static_cast<std::int32_t>(pos));
    }
    case Op::starts_with: return write(text.starts_with(needle));
    case Op::starts_with_from: {
        const auto start = num(2);
        return write(start >= 0 && start <= size && text.substr(static_cast<std::size_t>(start)).starts_with(needle));
    }
    case Op::ends_with: return write(text.ends_with(needle));
    default: break;
    }

    auto left = text;
    auto right = needle;
    bool ignore_case = true;
    if (op == Op::equals_ignore_case) {
        if (size != needle_size) return write(0);
    } else {
        const auto first = num(other_index - 1);
        const auto second = num(other_index + 1);
        const auto length = num(other_index + 2);
        if (first < 0 || second < 0 || length > size - first || length > needle_size - second) return write(0);
        if (length <= 0) {
            if (op == Op::region_matches_case && num(1) != 0) return false;
            return write(1);
        }
        left = text.substr(static_cast<std::size_t>(first), static_cast<std::size_t>(length));
        right = needle.substr(static_cast<std::size_t>(second), static_cast<std::size_t>(length));
        ignore_case = op == Op::region_matches_case && num(1) != 0;
    }
    if (op == Op::region_matches) return write(left == right);
    const auto ascii = [](char16_t ch) { return ch <= 0x7f; };
    if (!std::ranges::all_of(left, ascii) || !std::ranges::all_of(right, ascii)) return false;
    const auto fold = [](char16_t ch) { return ch >= u'A' && ch <= u'Z' ? ch + 32 : ch; };
    return write(ignore_case ? std::ranges::equal(left, right, {}, fold, fold) : left == right);
}

}  // namespace

void Interpreter::Impl::StepThreaded(InterpreterExecutionState& execution) {
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
    const auto dex_unit = *frame_ptr->method->dex_unit;
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
                             "dexvm thread stopped at teardown after " + std::to_string(ticks) + " ticks");
        }
        if (ticks > config.tick_budget) {
            stats.executed_instructions += executed;
            executed = 0;
            execution.ticks = ticks;
            throw DexVmError(DexVmErrorReason::budget_exhausted,
                             "dexvm tick budget exhausted after " + std::to_string(ticks) + " ticks");
        }
    };

#define OGPLAY_SYNC_EXECUTION()                                                                                        \
    do {                                                                                                               \
        stats.executed_instructions += executed;                                                                       \
        executed = 0;                                                                                                  \
        execution.ticks = ticks;                                                                                       \
    } while (0)

#define OGPLAY_RECORD_TRACE(...)                                                                                       \
    do {                                                                                                               \
        if (!trace_ring.empty()) {                                                                                     \
            execution.ticks = ticks;                                                                                   \
            RecordTrace(__VA_ARGS__);                                                                                  \
        }                                                                                                              \
    } while (0)

#define OGPLAY_FETCH_AND_TICK()                                                                                        \
    do {                                                                                                               \
        if ((ticks & 1023U) == 0U) {                                                                                   \
            OGPLAY_SYNC_EXECUTION();                                                                                   \
            owner->CheckExecutionDeadline();                                                                           \
            execution_lock.YieldToWaiter();                                                                            \
        }                                                                                                              \
        insn = &code_ptr->instructions[ip];                                                                            \
        frame_ptr->pc = insn->dex_pc;                                                                                  \
        if (insn->handler == FastHandler::bridge) goto bridge;                                                         \
        ticks += 1U;                                                                                                   \
        ++executed;                                                                                                    \
        if (execution.stop_requested.load(std::memory_order_relaxed)) {                                                \
            OGPLAY_SYNC_EXECUTION();                                                                                   \
            throw DexVmError(DexVmErrorReason::thread_stopped,                                                         \
                             "dexvm thread stopped at teardown after " + std::to_string(ticks) + " ticks");            \
        }                                                                                                              \
        if (ticks > config.tick_budget) {                                                                              \
            OGPLAY_SYNC_EXECUTION();                                                                                   \
            throw DexVmError(DexVmErrorReason::budget_exhausted,                                                       \
                             "dexvm tick budget exhausted after " + std::to_string(ticks) + " ticks");                 \
        }                                                                                                              \
        OGPLAY_RECORD_TRACE(DexVmTraceKind::instruction, execution, frame_ptr->method, insn->dex_pc, insn->opcode);    \
    } while (0)

#if defined(__GNUC__) && !defined(_MSC_VER)
// Computed goto (&&label / goto*) is this file's deliberate GCC/Clang dispatch
// path; -Wpedantic reports the extension at every handler label.
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wgnu-label-as-value"
#else
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#define OGPLAY_DISPATCH_AT()                                                                                           \
    do {                                                                                                               \
        OGPLAY_FETCH_AND_TICK();                                                                                       \
        goto* op_labels[insn->opcode];                                                                                 \
    } while (0)
#define OGPLAY_DISPATCH_NEXT()                                                                                         \
    do {                                                                                                               \
        ip += 1U;                                                                                                      \
        OGPLAY_DISPATCH_AT();                                                                                          \
    } while (0)
#define OGPLAY_DISPATCH_TO(idx)                                                                                        \
    do {                                                                                                               \
        ip = (idx);                                                                                                    \
        OGPLAY_DISPATCH_AT();                                                                                          \
    } while (0)
#else
#define OGPLAY_DISPATCH_AT() goto fetch_at
#define OGPLAY_DISPATCH_NEXT()                                                                                         \
    do {                                                                                                               \
        ip += 1U;                                                                                                      \
        goto fetch_at;                                                                                                 \
    } while (0)
#define OGPLAY_DISPATCH_TO(idx)                                                                                        \
    do {                                                                                                               \
        ip = (idx);                                                                                                    \
        goto fetch_at;                                                                                                 \
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
    case FastHandler::straight: goto straight;
    case FastHandler::object_checked:
    case FastHandler::object_fast: goto object;
    case FastHandler::invoke_checked:
    case FastHandler::invoke_fast: goto invoke;
    case FastHandler::bridge: goto bridge;
    default: FailCode("unknown FastHandler in threaded dispatch");
    }
#endif

straight:
    {
        Frame& frame = *frame_ptr;
        const FastInstruction& instruction = *insn;
        const auto opcode = instruction.opcode;
#include "interp_threaded_straight.inc"
    }

object:
    {
        Frame& frame = *frame_ptr;
        const FastCode& code = *code_ptr;
        const FastInstruction& instruction = *insn;
        const auto opcode = instruction.opcode;
#include "interp_threaded_object.inc"
    }

invoke:
    {
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
