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
    const auto& code = linker->FastCodeFor(frame.method->id);
    if (needs_build) {
        ++stats.fast_code_builds;
        stats.fast_code_bytes += code.storage_bytes;
    }
    const auto& instruction = code.instructions[code.IndexForDexPc(frame.pc)];

#if defined(__GNUC__) && !defined(_MSC_VER)
    static const void* const labels[] = {
        &&bridge, &&straight, &&object, &&invoke,
    };
    goto* labels[static_cast<std::size_t>(instruction.handler)];

straight:
object:
invoke:
bridge:
    Step(execution);
#else
    switch (instruction.handler) {
        case FastHandler::bridge:
        case FastHandler::straight:
        case FastHandler::object:
        case FastHandler::invoke:
            Step(execution);
            break;
    }
#endif
}

}  // namespace ogplay::runtime::dexvm
