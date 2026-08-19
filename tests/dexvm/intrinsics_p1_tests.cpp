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
              model, nullptr, ledger, {}) {
        interpreter.SetLogger(&logger);
    }

  [[nodiscard]] VmCallOutcome CallStatic(const std::string &name,
                                         const std::string &descriptor,
        std::vector<VmValue> arguments = {},
        const std::string &owner = "LP1;") {
        const auto java_class = linker.FindClass(owner);
        REQUIRE(java_class.has_value());
    const auto method = linker.FindDirectMethod(*java_class, name, descriptor);
        const std::string qualified = owner + "." + name;
        REQUIRE_MESSAGE(method.has_value(), qualified);
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

void ExpectThrow(const Vm& vm, const VmCallOutcome& outcome,
                 const std::string& descriptor, const std::string& message) {
  const std::string context = "expected " + descriptor + " but it returned";
  REQUIRE_MESSAGE(outcome.exception.IsValid(), context);
  CHECK(vm.linker.Class(outcome.exception_class).descriptor == descriptor);
  CHECK(outcome.exception_message == message);
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
    ExpectInt(vm.CallStatic("primitiveWrappers", "()I"), 107);
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

TEST_CASE("dexvm P1 Enum constants answer name/ordinal/hash/identity") {
  Vm vm;
  CHECK(vm.AsString(vm.CallStatic("nameOf", "()Ljava/lang/String;", {},
                                  "LP1Enum;")) == "SMALL");
  ExpectInt(vm.CallStatic("ordinals", "()I", {}, "LP1Enum;"), 1);
  CHECK(vm.AsString(vm.CallStatic("stringOf", "()Ljava/lang/String;", {},
                                  "LP1Enum;")) == "SMALL");
  ExpectInt(vm.CallStatic("equalsSelf", "()I", {}, "LP1Enum;"), 1);
  ExpectInt(vm.CallStatic("equalsOther", "()I", {}, "LP1Enum;"), 0);

  // AOSP Enum.hashCode: ordinal + name.hashCode (Java formula, UTF-16 units).
  std::int32_t name_hash = 0;
  for (const auto unit : std::u16string{u"SMALL"}) {
    name_hash = 31 * name_hash + static_cast<std::int32_t>(unit);
  }
  ExpectInt(vm.CallStatic("hashOfSmall", "()I", {}, "LP1Enum;"),
            0 + name_hash);

  // toString is not final: the subclass override wins virtual dispatch.
  CHECK(vm.AsString(vm.CallStatic("tagged", "()Ljava/lang/String;", {},
                                  "LP1EnumTagged;")) == "TAG!");
}

TEST_CASE("dexvm P1 Enum compareTo follows ordinals") {
  Vm vm;
  ExpectInt(vm.CallStatic("compare", "()I", {}, "LP1Enum;"), -1);
  ExpectInt(vm.CallStatic("compareBridge", "()I", {}, "LP1Enum;"), -1);
  ExpectThrow(vm, vm.CallStatic("compareBridgeMismatch", "()I", {},
                                "LP1Enum;"),
              "Ljava/lang/ClassCastException;",
              "argument is not an enum constant");
  ExpectThrow(vm, vm.CallStatic("compareNull", "()I", {}, "LP1Enum;"),
              "Ljava/lang/NullPointerException;",
              "ordinal of null enum constant");
}

TEST_CASE("dexvm P1 Enum getDeclaringClass and valueOf") {
  Vm vm;
  ExpectInt(vm.CallStatic("declaringIsOwn", "()I", {}, "LP1Enum;"), 1);

  // valueOf answers the live singleton, not a copy.
  const auto resolved = vm.CallStatic(
      "valueOfSmall", "()Ljava/lang/Enum;", {}, "LP1Enum;");
  REQUIRE_MESSAGE(!resolved.exception.IsValid(), resolved.exception_message);
  const auto constant = vm.CallStatic(
      "smallConstant", "()Ljava/lang/Enum;", {}, "LP1Enum;");
  REQUIRE_MESSAGE(!constant.exception.IsValid(), constant.exception_message);
  CHECK(resolved.value.ref == constant.value.ref);

  ExpectThrow(vm, vm.CallStatic("valueOfMissing", "()Ljava/lang/Enum;", {},
                                "LP1Enum;"),
              "Ljava/lang/IllegalArgumentException;",
              "NOPE is not a constant in P1Enum");
  ExpectThrow(vm, vm.CallStatic("valueOfNullName", "()Ljava/lang/Enum;", {},
                                "LP1Enum;"),
              "Ljava/lang/NullPointerException;", "name == null");
  ExpectThrow(vm, vm.CallStatic("valueOfNotEnum", "()Ljava/lang/Enum;", {},
                                "LP1Enum;"),
              "Ljava/lang/IllegalArgumentException;",
              "class java.lang.String is not an enum type");
}

TEST_CASE("dexvm P1 Enum clone is rejected like the platform") {
  Vm vm;
  ExpectThrow(vm, vm.CallStatic("cloneOfSmall", "()Ljava/lang/Object;", {},
                                "LP1Enum;"),
              "Ljava/lang/CloneNotSupportedException;",
              "Enums may not be cloned");
}

TEST_CASE("dexvm P1 enum values array clone is a shallow copy") {
    Vm vm;
    ExpectInt(vm.CallStatic("cloneValues", "()I", {}, "LP1Enum;"), 1);
}
