// java.* P1 intrinsic conformance (DVM-13). Pure-library expectations
// follow the class-library documentation; System.arraycopy checks follow
// AOSP vm/native/java_lang_System.cpp at the pinned baseline.

#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

std::vector<std::uint8_t> ReadFixture(const std::string& name) {
  const std::string path = std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

struct Vm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    Interpreter interpreter;

    Vm()
      : model(strings, arrays), linker(),
          interpreter(
              [this]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  linker.RegisterDex(ReadFixture("p1.dex"));
                  linker.Link();
                  return linker;
              }(),
              model, IntrinsicRegistry{}, nullptr, ledger, {}) {
        interpreter.RegisterCoreBuiltins();
        interpreter.SetLogger(&logger);
    }

  [[nodiscard]] VmCallOutcome CallStatic(const std::string &name,
                                         const std::string &descriptor,
        std::vector<VmValue> arguments = {}) {
        const auto java_class = linker.FindClass("LP1;");
        REQUIRE(java_class.has_value());
    const auto method = linker.FindDirectMethod(*java_class, name, descriptor);
        REQUIRE_MESSAGE(method.has_value(), name);
        return interpreter.Call(*method, arguments);
    }

    [[nodiscard]] std::string AsString(const VmCallOutcome& outcome) {
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        REQUIRE(outcome.value.kind == VmValue::Kind::ref);
        return interpreter.StringUtf8(outcome.value.ref);
    }
};

void ExpectInt(const VmCallOutcome& outcome, const std::int32_t expected) {
  REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    REQUIRE(outcome.value.kind == VmValue::Kind::cat1);
    CHECK(outcome.value.AsInt() == expected);
}

}  // namespace

TEST_CASE("dexvm P1 StringBuilder chain renders like the class library") {
    Vm vm;
  CHECK(vm.AsString(vm.CallStatic("buildString", "(I)Ljava/lang/String;",
                                    {VmValue::Int(42)})) == "value:42true");
}

TEST_CASE("dexvm P1 String surface: trim/lower/startsWith/indexOf") {
    Vm vm;
    ExpectInt(vm.CallStatic("stringOps", "()I"), 12);
  CHECK(vm.AsString(vm.CallStatic("substrings", "()Ljava/lang/String;")) ==
        "Rocks");
    ExpectInt(vm.CallStatic("compareIgnore", "()I"), 0);
}

TEST_CASE("dexvm P1 boxed values") {
    Vm vm;
    ExpectInt(vm.CallStatic("boxing", "()I"), 58);
    const auto boxed = vm.CallStatic("longBox", "()J");
    REQUIRE(boxed.value.kind == VmValue::Kind::wide);
    CHECK(boxed.value.AsLong() == 0x1234567800000000LL);
}

TEST_CASE("dexvm P1 collections use Java string equality for keys") {
    Vm vm;
    ExpectInt(vm.CallStatic("mapRoundTrip", "()I"), 99);
    ExpectInt(vm.CallStatic("vectorOps", "()I"), 4);
  ExpectInt(vm.CallStatic("stackOps", "()I"), 6);
}

TEST_CASE("dexvm P1 System.arraycopy and Math") {
    Vm vm;
    ExpectInt(vm.CallStatic("copyArrays", "()I"), 40);
    ExpectInt(vm.CallStatic("mathMix", "()I"), 10);
}

TEST_CASE("dexvm P1 System.out println reaches the structured logger") {
    Vm vm;
    const auto outcome = vm.CallStatic("printHello", "()V");
  REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
  const auto records = vm.logger.Snapshot(std::nullopt, "runtime.dexvm.guest");
    REQUIRE(!records.empty());
    CHECK(records.back().message == "hello from dexvm");
}

TEST_CASE("dexvm Object notify validates monitor ownership") {
  Vm vm;
  const auto owned = vm.CallStatic("notifyOwnedMonitor", "()V");
  CHECK_FALSE(owned.exception.IsValid());

  const auto unowned = vm.CallStatic("notifyUnownedMonitor", "()V");
  REQUIRE(unowned.exception.IsValid());
  CHECK(vm.linker.Class(unowned.exception_class).descriptor ==
        "Ljava/lang/IllegalMonitorStateException;");
}
