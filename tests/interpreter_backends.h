#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::test {

// DexVM backends that backend-parameterized tests iterate. Threaded is the production
// default and runs alone unless OGPLAY_TEST_BACKENDS selects "all" or "switch".
[[nodiscard]] inline const std::vector<runtime::dexvm::InterpreterBackend>& InterpreterBackends() {
    using runtime::dexvm::InterpreterBackend;
    static const std::vector<InterpreterBackend> backends = [] {
        const char* raw = std::getenv("OGPLAY_TEST_BACKENDS");
        const std::string_view value = raw ? raw : "threaded";
        if (value == "threaded") return std::vector{InterpreterBackend::threaded};
        if (value == "switch") return std::vector{InterpreterBackend::switch_dispatch};
        if (value == "all") {
            return std::vector{InterpreterBackend::switch_dispatch, InterpreterBackend::threaded};
        }
        throw std::invalid_argument("OGPLAY_TEST_BACKENDS must be threaded, switch or all: " + std::string(value));
    }();
    return backends;
}

}  // namespace ogplay::test
