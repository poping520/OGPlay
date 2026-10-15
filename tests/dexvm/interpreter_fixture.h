#pragma once

#include "boot_dex.h"
// DexVM stage-1 interpreter conformance over dexasm fixtures.
// Expected values are recorded with their semantic source: AOSP
// vm/mterp/c/OP_*.cpp at the pinned baseline (07 §2 mode B) or the Dalvik
// bytecode specification (docs/dalvik-bytecode at the same tag).

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/text.h"
#include "ogplay/hal/clock.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

namespace ogplay::test::interpreter {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
std::vector<std::uint8_t> ReadFixture(const std::string& name);

void ExpectInt(const VmCallOutcome& outcome, const std::int32_t expected);

struct Vm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;

    explicit Vm(const InterpreterConfig config = {},
                const JavaObjectModelConfig model_config = {},
                std::vector<IntrinsicClassDecl> extra_catalog = {},
                const std::string& extra_fixture = {});

    [[nodiscard]] VmMethodId Static(const std::string& class_descriptor,
                                    const std::string& name,
                                    const std::string& descriptor);

    [[nodiscard]] VmCallOutcome CallStatic(
        const std::string& class_descriptor, const std::string& name,
        const std::string& descriptor, std::vector<VmValue> arguments = {});
};

struct IntrinsicVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;

    explicit IntrinsicVm(std::vector<IntrinsicClassDecl> catalog);

    [[nodiscard]] VmMethodId Static(const std::string& class_descriptor,
                                    const std::string& name,
                                    const std::string& descriptor);

    [[nodiscard]] VmMethodId Virtual(const std::string& class_descriptor,
                                     const std::string& name,
                                     const std::string& descriptor);
};

template <typename Fn>
void WithEachBackend(Fn&& fn) {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        INFO("backend=",
             backend == InterpreterBackend::threaded ? "threaded" : "switch");
        InterpreterConfig config;
        config.backend = backend;
        fn(config);
    }
}

template <typename VmType>
void ExpectException(const VmType& vm, const VmCallOutcome& outcome,
                     const std::string& descriptor) {
    REQUIRE(outcome.exception.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor == descriptor);
}


} // namespace ogplay::test::interpreter
