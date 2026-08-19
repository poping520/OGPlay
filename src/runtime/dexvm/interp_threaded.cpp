// FastCode dispatcher. DVM-54 deliberately starts with an all-bridge table:
// the outer Run exception/unwind path stays single-source while later WUs
// replace individual handler families. GCC/Clang use labels-as-values;
// MSVC uses the equivalent dense enum switch.

#include "interpreter_internal.h"

namespace ogplay::runtime::dexvm {

void Interpreter::Impl::StepThreaded(
    InterpreterExecutionState& execution) {
    auto& frame = execution.frames.back();
    const bool needs_build = !frame.method->fast_code;
    if (needs_build) {
        static_cast<void>(linker->FastCodeFor(frame.method->id));
    }
    const auto& code = *frame.method->fast_code;
    if (needs_build) {
        ++stats.fast_code_builds;
        stats.fast_code_bytes += code.storage_bytes;
    }
    const auto& instruction = code.instructions[code.IndexForDexPc(frame.pc)];

#if defined(__GNUC__) && !defined(_MSC_VER)
    static const void* const labels[] = {
        &&bridge, &&straight, &&object_checked, &&object_fast,
        &&invoke_checked, &&invoke_fast,
    };
    goto* labels[static_cast<std::size_t>(instruction.handler)];

straight:
    StepStraight(execution, code, instruction);
    return;
object_checked:
object_fast:
    StepObject(execution, code, instruction);
    return;
invoke_checked:
invoke_fast:
    StepInvoke(execution, code, instruction);
    return;
bridge:
    Step(execution);
#else
    switch (instruction.handler) {
        case FastHandler::straight:
            StepStraight(execution, code, instruction);
            break;
        case FastHandler::object_checked:
        case FastHandler::object_fast:
            StepObject(execution, code, instruction);
            break;
        case FastHandler::invoke_checked:
        case FastHandler::invoke_fast:
            StepInvoke(execution, code, instruction);
            break;
        case FastHandler::bridge:
            Step(execution);
            break;
    }
#endif
}

}  // namespace ogplay::runtime::dexvm
