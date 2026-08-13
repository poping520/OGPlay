// DexVM stage-1 interpreter conformance over dexasm fixtures.
// Expected values are recorded with their semantic source: AOSP
// vm/mterp/c/OP_*.cpp at the pinned baseline (07 §2 mode B) or the Dalvik
// bytecode specification (docs/dalvik-bytecode at the same tag).

#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

std::vector<std::uint8_t> ReadFixture(const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
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
    Interpreter interpreter;

    explicit Vm(const InterpreterConfig config = {},
                const JavaObjectModelConfig model_config = {},
                const bool register_core_builtins = true)
        : model(strings, arrays, model_config),
          linker(),
          interpreter(
              [this]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  linker.RegisterDex(ReadFixture("interp.dex"));
                  linker.Link();
                  return linker;
              }(),
              model, IntrinsicRegistry{}, nullptr, ledger, config) {
        if (register_core_builtins) {
            interpreter.RegisterCoreBuiltins();
        }
    }

    [[nodiscard]] VmMethodId Static(const std::string& class_descriptor,
                                    const std::string& name,
                                    const std::string& descriptor) {
        const auto java_class = linker.FindClass(class_descriptor);
        REQUIRE_MESSAGE(java_class.has_value(), class_descriptor);
        const auto method =
            linker.FindDirectMethod(*java_class, name, descriptor);
        REQUIRE_MESSAGE(method.has_value(), name);
        return *method;
    }

    [[nodiscard]] VmCallOutcome CallStatic(
        const std::string& class_descriptor, const std::string& name,
        const std::string& descriptor, std::vector<VmValue> arguments = {}) {
        return interpreter.Call(Static(class_descriptor, name, descriptor),
                                arguments);
    }
};

struct IntrinsicVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;

    IntrinsicVm(std::vector<IntrinsicClassDecl> catalog,
                IntrinsicRegistry registry = {})
        : model(strings, arrays),
          linker(),
          interpreter(
              [this, &catalog]() -> DexClassLinker& {
                  auto core = CoreIntrinsicCatalog();
                  core.insert(core.end(),
                              std::make_move_iterator(catalog.begin()),
                              std::make_move_iterator(catalog.end()));
                  linker.RegisterIntrinsics(core);
                  linker.Link();
                  return linker;
              }(),
              model, std::move(registry), nullptr, ledger) {}

    [[nodiscard]] VmMethodId Static(const std::string& class_descriptor,
                                    const std::string& name,
                                    const std::string& descriptor) {
        const auto java_class = linker.FindClass(class_descriptor);
        REQUIRE(java_class.has_value());
        const auto method =
            linker.FindDirectMethod(*java_class, name, descriptor);
        REQUIRE(method.has_value());
        return *method;
    }
};

void ExpectInt(const VmCallOutcome& outcome, const std::int32_t expected) {
    REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                    "unexpected exception: ", outcome.exception_message);
    REQUIRE(outcome.value.kind == VmValue::Kind::cat1);
    CHECK(outcome.value.AsInt() == expected);
}

template <typename VmType>
void ExpectException(const VmType& vm, const VmCallOutcome& outcome,
                     const std::string& descriptor) {
    REQUIRE(outcome.exception.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor == descriptor);
}

}  // namespace

TEST_CASE("dexvm intrinsic registry preserves duplicate rejection") {
    IntrinsicRegistry registry;
    registry.Register("test.duplicate", [](IntrinsicContext&) {
        return VmValue::Void();
    });

    bool rejected = false;
    try {
        registry.Register("test.duplicate", [](IntrinsicContext&) {
            return VmValue::Void();
        });
    } catch (const DexVmError& error) {
        rejected = true;
        CHECK(error.Reason() == DexVmErrorReason::internal_invariant);
        CHECK(std::string(error.what()).find("registered twice") !=
              std::string::npos);
    }
    CHECK(rejected);
}

TEST_CASE("dexvm intrinsic registry freezes at first execution") {
    Vm before_call(InterpreterConfig{}, JavaObjectModelConfig{}, false);
    CHECK_NOTHROW(before_call.interpreter.RegisterCoreBuiltins());
    ExpectInt(before_call.interpreter.Call(
                  before_call.Static("Ljava/lang/Math;", "abs", "(I)I"),
                  std::vector<VmValue>{VmValue::Int(-7)}),
              7);

    Vm after_call(InterpreterConfig{}, JavaObjectModelConfig{}, false);
    ExpectInt(after_call.CallStatic("LArith;", "divide", "(II)I",
                                    {VmValue::Int(8), VmValue::Int(2)}),
              4);
    bool rejected = false;
    try {
        after_call.interpreter.RegisterCoreBuiltins();
    } catch (const DexVmError& error) {
        rejected = true;
        CHECK(error.Reason() == DexVmErrorReason::internal_invariant);
        CHECK(std::string(error.what()).find("registry is frozen") !=
              std::string::npos);
    }
    CHECK(rejected);
}

TEST_CASE("dexvm intrinsic handlers bind once and preserve repeated misses") {
    Vm hit;
    const auto hit_method =
        hit.Static("Ljava/lang/Math;", "abs", "(I)I");
    CHECK_FALSE(hit.linker.Method(hit_method).handler_bound);
    ExpectInt(hit.interpreter.Call(
                  hit_method, std::vector<VmValue>{VmValue::Int(-9)}),
              9);
    const auto* resolved = hit.linker.Method(hit_method).resolved_handler;
    REQUIRE(hit.linker.Method(hit_method).handler_bound);
    REQUIRE(resolved != nullptr);
    ExpectInt(hit.interpreter.Call(
                  hit_method, std::vector<VmValue>{VmValue::Int(-11)}),
              11);
    CHECK(hit.linker.Method(hit_method).resolved_handler == resolved);

    Vm miss(InterpreterConfig{}, JavaObjectModelConfig{}, false);
    const auto miss_method =
        miss.Static("Ljava/lang/Math;", "abs", "(I)I");
    ExpectException(
        miss,
        miss.interpreter.Call(
            miss_method, std::vector<VmValue>{VmValue::Int(-1)}),
        "Ljava/lang/UnsatisfiedLinkError;");
    REQUIRE(miss.linker.Method(miss_method).handler_bound);
    CHECK(miss.linker.Method(miss_method).resolved_handler == nullptr);
    ExpectException(
        miss,
        miss.interpreter.Call(
            miss_method, std::vector<VmValue>{VmValue::Int(-2)}),
        "Ljava/lang/UnsatisfiedLinkError;");

    const auto hits = miss.ledger.Unimplemented();
    bool found = false;
    for (const auto& entry : hits) {
        if (entry.id == "dexvm.intrinsic.Ljava/lang/Math;.abs(I)I") {
            found = true;
            CHECK(entry.count == 2U);
        }
    }
    CHECK(found);
}

TEST_CASE("dexvm intrinsic builder binds implementations without a registry") {
    std::uint32_t clinit_calls = 0;
    IntrinsicClassBuilder builder("Lbuilder/Direct;");
    builder.Super("Ljava/lang/Object;")
        .Static("answer", "()I", [](IntrinsicContext&) {
            return VmValue::Int(42);
        })
        .Virtual("virtualAnswer", "()I", [](IntrinsicContext&) {
            return VmValue::Int(43);
        })
        .Overridable("overridableAnswer", "()I", [](IntrinsicContext&) {
            return VmValue::Int(44);
        })
        .ConstantInt("COUNT", "I", 41)
        .ConstantString("NAME", "direct")
        .Clinit([&clinit_calls](IntrinsicContext&) {
            ++clinit_calls;
            return VmValue::Void();
        });
    std::vector<IntrinsicClassDecl> catalog;
    catalog.push_back(std::move(builder).Build());
    IntrinsicVm vm(std::move(catalog));

    ExpectInt(vm.interpreter.Call(vm.Static("Lbuilder/Direct;", "answer",
                                            "()I"),
                                  {}),
              42);
    CHECK(clinit_calls == 1U);
    ExpectInt(vm.interpreter.Call(vm.Static("Lbuilder/Direct;", "answer",
                                            "()I"),
                                  {}),
              42);
    CHECK(clinit_calls == 1U);

    const auto java_class = vm.linker.FindClass("Lbuilder/Direct;");
    REQUIRE(java_class.has_value());
    const auto instance =
        vm.interpreter.NewIntrinsicInstance("Lbuilder/Direct;");
    for (const auto& [name, expected] :
         std::vector<std::pair<std::string, std::int32_t>>{
             {"virtualAnswer", 43}, {"overridableAnswer", 44}}) {
        const auto index =
            vm.linker.FindVtableIndex(*java_class, name, "()I");
        REQUIRE(index.has_value());
        ExpectInt(vm.interpreter.Call(
                      vm.linker.Class(*java_class).vtable[*index],
                      std::vector<VmValue>{VmValue::Ref(instance)}),
                  expected);
    }

    const auto count = vm.linker.FindFieldRecursive(*java_class, "COUNT", "I");
    REQUIRE(count.has_value());
    CHECK(vm.linker.Class(*java_class).static_storage[
              vm.linker.Field(*count).slot] == 41U);
    const auto name = vm.linker.FindFieldRecursive(
        *java_class, "NAME", "Ljava/lang/String;");
    REQUIRE(name.has_value());
    const auto name_ref = VmObjectRef(vm.linker.Class(*java_class)
                                           .static_storage[
                                               vm.linker.Field(*name).slot]);
    CHECK(vm.interpreter.StringUtf8(name_ref) == "direct");
}

TEST_CASE("dexvm intrinsic builder rejects invalid declarations at build") {
    auto expect_invalid = [](IntrinsicClassBuilder builder) {
        bool rejected = false;
        try {
            static_cast<void>(std::move(builder).Build());
        } catch (const DexVmError& error) {
            rejected = true;
            CHECK(error.Reason() == DexVmErrorReason::internal_invariant);
        }
        CHECK(rejected);
    };

    IntrinsicClassBuilder duplicate("Lbuilder/Duplicate;");
    duplicate.Static("answer", "()I", {})
        .Static("answer", "()I", {});
    expect_invalid(std::move(duplicate));

    IntrinsicClassBuilder invalid_descriptor("Lbuilder/Invalid;");
    invalid_descriptor.Static("answer", "(I", {});
    expect_invalid(std::move(invalid_descriptor));

    IntrinsicClassBuilder invalid_interface("Lbuilder/Interface;");
    invalid_interface.MarkInterface().Field("state", "I", false);
    expect_invalid(std::move(invalid_interface));
}

TEST_CASE("dexvm declaration miss uses the owner signature and repeats") {
    IntrinsicClassBuilder builder("Lbuilder/Missing;");
    builder.Super("Ljava/lang/Object;").Static("answer", "()I", {});
    std::vector<IntrinsicClassDecl> catalog;
    catalog.push_back(std::move(builder).Build());
    IntrinsicVm vm(std::move(catalog));
    const auto method = vm.Static("Lbuilder/Missing;", "answer", "()I");

    for (std::uint32_t call = 0; call < 2; ++call) {
        ExpectException(vm, vm.interpreter.Call(method, {}),
                        "Ljava/lang/UnsatisfiedLinkError;");
    }
    const auto hits = vm.ledger.Unimplemented();
    REQUIRE(hits.size() == 1U);
    CHECK(hits[0].id ==
          "dexvm.intrinsic.Lbuilder/Missing;.answer()I");
    CHECK(hits[0].count == 2U);
}

TEST_CASE("dexvm direct and string intrinsic channels coexist") {
    IntrinsicClassBuilder direct("Lbuilder/NewChannel;");
    direct.Super("Ljava/lang/Object;")
        .Static("answer", "()I", [](IntrinsicContext&) {
            return VmValue::Int(51);
        });

    IntrinsicClassDecl legacy;
    legacy.descriptor = "Lbuilder/OldChannel;";
    legacy.superclass = "Ljava/lang/Object;";
    legacy.methods.push_back(
        {"answer", "()I", true, false, "legacy.answer"});
    std::vector<IntrinsicClassDecl> catalog;
    catalog.push_back(std::move(direct).Build());
    catalog.push_back(std::move(legacy));
    IntrinsicRegistry registry;
    registry.Register("legacy.answer", [](IntrinsicContext&) {
        return VmValue::Int(52);
    });
    IntrinsicVm vm(std::move(catalog), std::move(registry));

    ExpectInt(vm.interpreter.Call(
                  vm.Static("Lbuilder/NewChannel;", "answer", "()I"), {}),
              51);
    ExpectInt(vm.interpreter.Call(
                  vm.Static("Lbuilder/OldChannel;", "answer", "()I"), {}),
              52);
}

TEST_CASE("dexvm arithmetic edge semantics") {
    Vm vm;
    // OP_DIV_INT.cpp: MIN_INT / -1 == MIN_INT (no trap).
    ExpectInt(vm.CallStatic("LArith;", "divide", "(II)I",
                            {VmValue::Int(std::numeric_limits<
                                          std::int32_t>::min()),
                             VmValue::Int(-1)}),
              std::numeric_limits<std::int32_t>::min());
    ExpectInt(vm.CallStatic("LArith;", "divide", "(II)I",
                            {VmValue::Int(7), VmValue::Int(-2)}),
              -3);
    // OP_REM_INT.cpp: MIN_INT % -1 == 0.
    ExpectInt(vm.CallStatic("LArith;", "remainder", "(II)I",
                            {VmValue::Int(std::numeric_limits<
                                          std::int32_t>::min()),
                             VmValue::Int(-1)}),
              0);
    // OP_DIV_INT.cpp: divide by zero throws ArithmeticException.
    const auto division = vm.CallStatic(
        "LArith;", "divide", "(II)I", {VmValue::Int(1), VmValue::Int(0)});
    ExpectException(vm, division, "Ljava/lang/ArithmeticException;");
    // In-method catch handler observes the exception.
    ExpectInt(vm.CallStatic("LArith;", "divideCaught", "(II)I",
                            {VmValue::Int(1), VmValue::Int(0)}),
              -99);
    // OP_CMPL_FLOAT.cpp: NaN biases to -1; OP_CMPG_FLOAT.cpp biases to +1.
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    ExpectInt(vm.CallStatic("LArith;", "cmplFloat", "(FF)I",
                            {VmValue::Float(nan), VmValue::Float(0.0f)}),
              -1);
    ExpectInt(vm.CallStatic("LArith;", "cmpgFloat", "(FF)I",
                            {VmValue::Float(nan), VmValue::Float(0.0f)}),
              1);
    ExpectInt(vm.CallStatic("LArith;", "cmplFloat", "(FF)I",
                            {VmValue::Float(2.0f), VmValue::Float(1.0f)}),
              1);
    // OP_SHL_INT.cpp: shift distance masks to 5 bits (33 -> 1).
    ExpectInt(vm.CallStatic("LArith;", "shifts", "(II)I",
                            {VmValue::Int(3), VmValue::Int(33)}),
              6);
    // OP_USHR_INT.cpp: logical shift of negative operand.
    ExpectInt(vm.CallStatic("LArith;", "ushr", "(II)I",
                            {VmValue::Int(-1), VmValue::Int(28)}),
              15);
    // OP_FLOAT_TO_INT.cpp: NaN -> 0, +inf -> MAX_INT.
    ExpectInt(vm.CallStatic("LArith;", "floatToInt", "(F)I",
                            {VmValue::Float(nan)}),
              0);
    ExpectInt(vm.CallStatic("LArith;", "floatToInt", "(F)I",
                            {VmValue::Float(
                                std::numeric_limits<float>::infinity())}),
              std::numeric_limits<std::int32_t>::max());
    // OP_INT_TO_SHORT.cpp: truncation with sign extension.
    ExpectInt(vm.CallStatic("LArith;", "intToShort", "(I)I",
                            {VmValue::Int(0x18000)}),
              static_cast<std::int32_t>(
                  static_cast<std::int16_t>(0x8000)));
    // OP_MUL_LONG.cpp: 64-bit wraparound multiply.
    const auto product = vm.CallStatic(
        "LArith;", "longMul", "(JJ)J",
        {VmValue::Long(0x100000001LL), VmValue::Long(3)});
    REQUIRE(product.value.kind == VmValue::Kind::wide);
    CHECK(product.value.AsLong() == 0x300000003LL);
}

TEST_CASE("dexvm control flow and switches") {
    Vm vm;
    ExpectInt(vm.CallStatic("LFlow;", "loopSum", "(I)I", {VmValue::Int(10)}),
              45);
    ExpectInt(vm.CallStatic("LFlow;", "pick", "(I)I", {VmValue::Int(1)}), 1);
    ExpectInt(vm.CallStatic("LFlow;", "pick", "(I)I", {VmValue::Int(2)}), 22);
    ExpectInt(vm.CallStatic("LFlow;", "pick", "(I)I", {VmValue::Int(9)}), 0);
    ExpectInt(vm.CallStatic("LFlow;", "sparse", "(I)I", {VmValue::Int(-5)}),
              111);
    ExpectInt(vm.CallStatic("LFlow;", "sparse", "(I)I",
                            {VmValue::Int(1000)}),
              222);
    ExpectInt(vm.CallStatic("LFlow;", "sparse", "(I)I", {VmValue::Int(3)}),
              -1);
}

TEST_CASE("dexvm arrays and implicit exceptions") {
    Vm vm;
    ExpectInt(vm.CallStatic("LFlow;", "sumArray", "()I"), 24);
    ExpectException(vm, vm.CallStatic("LFlow;", "outOfBounds", "()I"),
                    "Ljava/lang/ArrayIndexOutOfBoundsException;");
    ExpectException(vm, vm.CallStatic("LFlow;", "npe", "()I"),
                    "Ljava/lang/NullPointerException;");
}

TEST_CASE("dexvm objects, fields and dispatch") {
    Vm vm;
    // Instance fields via <init>/iget/iput and virtual dispatch.
    ExpectInt(vm.CallStatic("LTask;", "exercise", "()I"), 77);
    ExpectInt(vm.CallStatic("LTypes;", "isRunnable", "()I"), 1);
    ExpectException(vm, vm.CallStatic("LTypes;", "badCast", "()V"),
                    "Ljava/lang/ClassCastException;");
    ExpectInt(vm.CallStatic("LTypes;", "strings", "()I"), 6);
}

TEST_CASE("dexvm virtual and super dispatch through subclass") {
    Vm vm;
    // Construct LDoubler and call describe() virtually via LCounter ref.
    const auto doubler_class = vm.linker.FindClass("LDoubler;");
    REQUIRE(doubler_class.has_value());
    const auto init =
        vm.linker.FindDirectMethod(*doubler_class, "<init>", "(I)V");
    REQUIRE(init.has_value());
    const auto instance = vm.model.NewInstance(
        *doubler_class, vm.linker.Class(*doubler_class).instance_slots);
    const auto construct = vm.interpreter.Call(
        *init, std::vector<VmValue>{VmValue::Ref(instance), VmValue::Int(4)});
    REQUIRE(!construct.exception.IsValid());

    const auto index = vm.linker.FindVtableIndex(*doubler_class, "describe",
                                                 "()I");
    REQUIRE(index.has_value());
    const auto target = vm.linker.Class(*doubler_class).vtable[*index];
    const auto outcome = vm.interpreter.Call(
        target, std::vector<VmValue>{VmValue::Ref(instance)});
    ExpectInt(outcome, 200);  // super 100 * 2 via invoke-super

    // get() reads the field written by the chained constructors.
    const auto get_index =
        vm.linker.FindVtableIndex(*doubler_class, "get", "()I");
    REQUIRE(get_index.has_value());
    ExpectInt(vm.interpreter.Call(
                  vm.linker.Class(*doubler_class).vtable[*get_index],
                  std::vector<VmValue>{VmValue::Ref(instance)}),
              4);
}

TEST_CASE("dexvm clinit runs once before static access") {
    Vm vm;
    ExpectInt(vm.CallStatic("LClinitUser;", "read", "()I"), 55);
    CHECK(vm.interpreter.Stats().classes_initialized >= 1);
    const auto before = vm.interpreter.Stats().classes_initialized;
    ExpectInt(vm.CallStatic("LClinitUser;", "read", "()I"), 55);
    CHECK(vm.interpreter.Stats().classes_initialized == before);
    // Static initial value from encoded_array.
    const auto counter_class = vm.linker.FindClass("LCounter;");
    REQUIRE(counter_class.has_value());
    const auto outcome =
        vm.interpreter.EnsureClassInitialized(*counter_class);
    REQUIRE(!outcome.exception.IsValid());
    CHECK(vm.linker.Class(*counter_class).static_storage[0] == 7);
}

TEST_CASE("dexvm exceptions across frames with real messages") {
    Vm vm;
    // Custom VM exception subclass: caught by Exception handler in the
    // caller frame; message length of "boom" is 4.
    ExpectInt(vm.CallStatic("LThrower;", "catchAcrossFrames", "()I"), 4);

    const auto uncaught = vm.CallStatic("LThrower;", "uncaught", "()I");
    ExpectException(vm, uncaught, "LMyError;");
    CHECK(uncaught.exception_message == "boom");
    REQUIRE(!uncaught.exception_stack.empty());
    CHECK(uncaught.exception_stack[0].method_name == "fail");
}

TEST_CASE("dexvm stack overflow is a real catchable error") {
    Vm vm;
    const auto outcome =
        vm.CallStatic("LFlow;", "recurse", "(I)I", {VmValue::Int(0)});
    ExpectException(vm, outcome, "Ljava/lang/StackOverflowError;");
}

TEST_CASE("dexvm tick budget exhaustion is a fatal structured error") {
    InterpreterConfig config;
    config.tick_budget = 100;
    Vm vm(config);
    CHECK_THROWS_AS(static_cast<void>(vm.CallStatic(
                        "LFlow;", "recurse", "(I)I", {VmValue::Int(0)})),
                    DexVmError);
}

TEST_CASE("dexvm heap budget exhaustion surfaces OutOfMemoryError") {
    JavaObjectModelConfig model_config;
    model_config.heap_budget_bytes = 40;
    Vm vm(InterpreterConfig{}, model_config);
    const auto outcome = vm.CallStatic("LFlow;", "sumArray", "()I");
    ExpectException(vm, outcome, "Ljava/lang/OutOfMemoryError;");
}

TEST_CASE("dexvm execution contexts isolate mutable interpreter state") {
    Vm vm;
    const auto first = vm.interpreter.CreateExecutionContext();
    const auto second = vm.interpreter.CreateExecutionContext();

    const auto loop = vm.Static("LFlow;", "loopSum", "(I)I");
    ExpectInt(vm.interpreter.Call(
                  first, loop, std::vector<VmValue>{VmValue::Int(10)}),
              45);
    const auto first_after_loop = vm.interpreter.ExecutionSnapshot(first);
    CHECK(first_after_loop.frame_depth == 0);
    CHECK(first_after_loop.ticks > 0);

    ExpectInt(vm.interpreter.Call(
                  second, loop, std::vector<VmValue>{VmValue::Int(3)}),
              3);
    const auto second_after_loop = vm.interpreter.ExecutionSnapshot(second);
    CHECK(second_after_loop.frame_depth == 0);
    CHECK(second_after_loop.ticks > 0);
    CHECK(vm.interpreter.ExecutionSnapshot(first).ticks ==
          first_after_loop.ticks);

    const auto uncaught = vm.interpreter.Call(
        first, vm.Static("LThrower;", "uncaught", "()I"), {});
    ExpectException(vm, uncaught, "LMyError;");
    CHECK(!vm.interpreter.ExecutionSnapshot(first).has_pending_exception);
    ExpectInt(vm.interpreter.Call(
                  second, loop, std::vector<VmValue>{VmValue::Int(2)}),
              1);
    CHECK(!vm.interpreter.ExecutionSnapshot(second).has_pending_exception);

    const auto retain = vm.Static("LExecutionContextProbe;", "retainMonitor",
                                  "()V");
    const auto retained = vm.interpreter.Call(first, retain, {});
    CHECK(!retained.exception.IsValid());
    CHECK(vm.interpreter.ExecutionSnapshot(first).held_monitor_count == 1);
    CHECK(vm.interpreter.ExecutionSnapshot(second).held_monitor_count == 0);

    Vm other;
    CHECK_THROWS_AS(
        static_cast<void>(other.interpreter.ExecutionSnapshot(first)),
        DexVmError);
}
