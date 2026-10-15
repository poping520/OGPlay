#include "interpreter_fixture.h"

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



std::vector<std::uint8_t> ReadFixture(const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

void ExpectInt(const VmCallOutcome& outcome, const std::int32_t expected) {
    REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                    "unexpected exception: ", outcome.exception_message);
    REQUIRE(outcome.value.kind == VmValue::Kind::cat1);
    CHECK(outcome.value.AsInt() == expected);
}

Vm::Vm(const InterpreterConfig config,
            const JavaObjectModelConfig model_config,
            std::vector<IntrinsicClassDecl> extra_catalog,
            const std::string& extra_fixture)
    : model(strings, arrays, model_config),
      linker(),
      interpreter(
          [this, &extra_catalog, &extra_fixture]() -> DexClassLinker& {
              auto catalog = CoreIntrinsicCatalog();
              catalog.insert(
                  catalog.end(),
                  std::make_move_iterator(extra_catalog.begin()),
                  std::make_move_iterator(extra_catalog.end()));
              linker.RegisterIntrinsics(catalog);
              linker.RegisterDex(ReadFixture(extra_fixture.empty() ? "interp.dex" : extra_fixture));
              ogplay::test::RegisterBootDex(linker);
              linker.Link();
              return linker;
          }(),
          model, nullptr, ledger, config) {}

VmMethodId Vm::Static(const std::string& class_descriptor,
                                const std::string& name,
                                const std::string& descriptor) {
    const auto java_class = linker.FindClass(class_descriptor);
    REQUIRE_MESSAGE(java_class.has_value(), class_descriptor);
    const auto method =
        linker.FindDirectMethod(*java_class, name, descriptor);
    REQUIRE_MESSAGE(method.has_value(), name);
    return *method;
}

VmCallOutcome Vm::CallStatic(
    const std::string& class_descriptor, const std::string& name,
    const std::string& descriptor, std::vector<VmValue> arguments) {
    return interpreter.Call(Static(class_descriptor, name, descriptor),
                            arguments);
}

IntrinsicVm::IntrinsicVm(std::vector<IntrinsicClassDecl> catalog)
    : model(strings, arrays),
      linker(),
      interpreter(
          [this, &catalog]() -> DexClassLinker& {
              auto core = CoreIntrinsicCatalog();
              core.insert(core.end(),
                          std::make_move_iterator(catalog.begin()),
                          std::make_move_iterator(catalog.end()));
              linker.RegisterIntrinsics(core);
              ogplay::test::RegisterBootDex(linker);
              linker.Link();
              return linker;
          }(),
          model, nullptr, ledger) {}

VmMethodId IntrinsicVm::Static(const std::string& class_descriptor,
                                const std::string& name,
                                const std::string& descriptor) {
    const auto java_class = linker.FindClass(class_descriptor);
    REQUIRE(java_class.has_value());
    const auto method =
        linker.FindDirectMethod(*java_class, name, descriptor);
    REQUIRE(method.has_value());
    return *method;
}

VmMethodId IntrinsicVm::Virtual(const std::string& class_descriptor,
                                 const std::string& name,
                                 const std::string& descriptor) {
    const auto java_class = linker.FindClass(class_descriptor);
    REQUIRE(java_class.has_value());
    for (const auto method : linker.Class(*java_class).vtable) {
        const auto& linked = linker.Method(method);
        if (linked.name == name && linked.descriptor == descriptor) {
            return method;
        }
    }
    FAIL("virtual method is missing: ", class_descriptor, ".", name,
         descriptor);
    return VmMethodId{};
}

} // namespace ogplay::test::interpreter
