#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

struct Dvm87Vm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter vm;
    VmThreadRuntime threads;

    Dvm87Vm()
        : vm([this]() -> DexClassLinker& {
              CoreIntrinsicServices services;
              services.current_time_millis = [] { return 1704067200000LL; };
              linker.RegisterIntrinsics(CoreIntrinsicCatalog(services));
              auto callable = IntrinsicClassBuilder::Class(
                  "Ltest/Dvm87Callable;", "Ljava/lang/Object;",
                  {"Ljava/util/concurrent/Callable;"});
              callable.VirtualMethod(
                  "call", "()Ljava/lang/Object;",
                  [](IntrinsicContext& context) {
                      return VmValue::Ref(context.vm.NewStringUtf8("done"));
                  });
              std::vector<IntrinsicClassDecl> test_catalog;
              test_catalog.push_back(std::move(callable).Build());
              linker.RegisterIntrinsics(test_catalog);
              linker.Link();
              return linker;
          }(), model, nullptr, ledger, {}),
          threads(vm) {}

    ~Dvm87Vm() { threads.Shutdown(); }

    [[nodiscard]] VmCallOutcome Static(
        const std::string_view owner, const std::string_view name,
        const std::string_view descriptor,
        std::vector<VmValue> arguments = {}) {
        const auto method = linker.FindDirectMethod(
            linker.ResolveDescriptor(owner), std::string(name),
            std::string(descriptor));
        REQUIRE(method.has_value());
        return vm.Call(*method, arguments);
    }

    [[nodiscard]] VmCallOutcome Virtual(
        const VmObjectRef receiver, const std::string_view name,
        const std::string_view descriptor,
        std::vector<VmValue> arguments = {}) {
        const auto owner = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(
            owner, std::string(name), std::string(descriptor));
        REQUIRE(index.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return vm.Call(linker.Class(owner).vtable[*index], arguments);
    }

    void Construct(const VmObjectRef object, const std::string_view owner,
                   const std::string_view descriptor,
                   std::vector<VmValue> arguments = {}) {
        arguments.insert(arguments.begin(), VmValue::Ref(object));
        RequireOk(Static(owner, "<init>", descriptor, arguments));
    }

    static void RequireOk(const VmCallOutcome& outcome) {
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    }
};

}  // namespace

TEST_CASE("DVM-87 Arrays primitive algorithms are deterministic") {
    Dvm87Vm fixture;
    const auto array = fixture.model.NewPrimitiveArray(
        fixture.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 4);
    for (std::int32_t index = 0; index < 4; ++index) {
        constexpr std::int32_t values[]{7, -2, 7, 3};
        fixture.model.SetPrimitiveElement(array, index, values[index]);
    }

    Dvm87Vm::RequireOk(fixture.Static(
        "Ljava/util/Arrays;", "sort", "([I)V", {VmValue::Ref(array)}));
    CHECK(static_cast<std::int32_t>(
              fixture.model.GetPrimitiveElement(array, 0)) == -2);
    CHECK(fixture.model.GetPrimitiveElement(array, 1) == 3);
    CHECK(fixture.model.GetPrimitiveElement(array, 2) == 7);
    CHECK(fixture.Static(
        "Ljava/util/Arrays;", "binarySearch", "([II)I",
        {VmValue::Ref(array), VmValue::Int(3)}).value.AsInt() == 1);

    const auto object_array = fixture.model.NewObjectArray(
        fixture.linker.ResolveDescriptor("[Ljava/lang/Object;"),
        fixture.linker.ResolveDescriptor("Ljava/lang/Object;"), 3);
    const auto first = fixture.vm.NewStringUtf8("first");
    const auto second = fixture.vm.NewStringUtf8("second");
    fixture.model.SetObjectElement(object_array, 0, first);
    fixture.model.SetObjectElement(object_array, 1, second);
    fixture.model.SetObjectElement(object_array, 2, second);
    const auto list = fixture.Static(
        "Ljava/util/Arrays;", "asList",
        "([Ljava/lang/Object;)Ljava/util/List;",
        {VmValue::Ref(object_array)}).value.ref;
    Dvm87Vm::RequireOk(fixture.Static(
        "Ljava/util/Collections;", "reverse", "(Ljava/util/List;)V",
        {VmValue::Ref(list)}));
    CHECK(fixture.model.GetObjectElement(object_array, 0) == second);
    CHECK(fixture.model.GetObjectElement(object_array, 1) == second);
    CHECK(fixture.model.GetObjectElement(object_array, 2) == first);
    CHECK(fixture.Static(
        "Ljava/util/Collections;", "frequency",
        "(Ljava/util/Collection;Ljava/lang/Object;)I",
        {VmValue::Ref(list), VmValue::Ref(second)}).value.AsInt() == 2);
}

TEST_CASE("DVM-87 Pattern Matcher supports find group and replacement") {
    Dvm87Vm fixture;
    const auto expression = fixture.vm.NewStringUtf8("a+");
    const auto pattern = fixture.Static(
        "Ljava/util/regex/Pattern;", "compile",
        "(Ljava/lang/String;)Ljava/util/regex/Pattern;",
        {VmValue::Ref(expression)}).value.ref;
    const auto input = fixture.vm.NewStringUtf8("xxaaay");
    const auto matcher = fixture.Virtual(
        pattern, "matcher",
        "(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;",
        {VmValue::Ref(input)}).value.ref;

    CHECK(fixture.Virtual(matcher, "find", "()Z").value.AsInt() == 1);
    CHECK(fixture.Virtual(matcher, "start", "()I").value.AsInt() == 2);
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(
        matcher, "group", "()Ljava/lang/String;").value.ref) == "aaa");
    const auto replaced = fixture.Virtual(
        matcher, "replaceAll", "(Ljava/lang/String;)Ljava/lang/String;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("_"))});
    CHECK(fixture.vm.StringUtf8(replaced.value.ref) == "xx_y");
}

TEST_CASE("DVM-87 Calendar uses injected clock and fixed-offset zones") {
    Dvm87Vm fixture;
    const auto calendar = fixture.Static(
        "Ljava/util/Calendar;", "getInstance",
        "()Ljava/util/Calendar;").value.ref;
    CHECK(fixture.Virtual(calendar, "getTimeInMillis", "()J").value.AsLong() ==
          1704067200000LL);
    CHECK(fixture.Virtual(
        calendar, "get", "(I)I", {VmValue::Int(1)}).value.AsInt() == 2024);
    CHECK(fixture.Virtual(
        calendar, "get", "(I)I", {VmValue::Int(2)}).value.AsInt() == 0);

    const auto zone = fixture.Static(
        "Ljava/util/TimeZone;", "getTimeZone",
        "(Ljava/lang/String;)Ljava/util/TimeZone;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("GMT+08:00"))}).value.ref;
    CHECK(fixture.Virtual(zone, "getRawOffset", "()I").value.AsInt() ==
          8 * 60 * 60 * 1000);
}

TEST_CASE("DVM-87 FutureTask and atomic state expose core semantics") {
    Dvm87Vm fixture;
    const auto callable = fixture.vm.NewIntrinsicInstance(
        "Ltest/Dvm87Callable;");
    const auto future = fixture.vm.NewIntrinsicInstance(
        "Ljava/util/concurrent/FutureTask;");
    fixture.Construct(
        future, "Ljava/util/concurrent/FutureTask;",
        "(Ljava/util/concurrent/Callable;)V", {VmValue::Ref(callable)});
    Dvm87Vm::RequireOk(fixture.Virtual(future, "run", "()V"));
    CHECK(fixture.Virtual(future, "isDone", "()Z").value.AsInt() == 1);
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(
        future, "get", "()Ljava/lang/Object;").value.ref) == "done");

    const auto atomic = fixture.vm.NewIntrinsicInstance(
        "Ljava/util/concurrent/atomic/AtomicInteger;");
    fixture.Construct(atomic, "Ljava/util/concurrent/atomic/AtomicInteger;",
                      "(I)V", {VmValue::Int(4)});
    CHECK(fixture.Virtual(
        atomic, "compareAndSet", "(II)Z",
        {VmValue::Int(4), VmValue::Int(9)}).value.AsInt() == 1);
    CHECK(fixture.Virtual(atomic, "incrementAndGet", "()I").value.AsInt() ==
          10);

    const auto executor = fixture.Static(
        "Ljava/util/concurrent/Executors;", "newSingleThreadExecutor",
        "()Ljava/util/concurrent/ExecutorService;").value.ref;
    const auto submitted = fixture.Virtual(
        executor, "submit",
        "(Ljava/util/concurrent/Callable;)Ljava/util/concurrent/Future;",
        {VmValue::Ref(callable)}).value.ref;
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(
        submitted, "get", "()Ljava/lang/Object;").value.ref) == "done");
    Dvm87Vm::RequireOk(fixture.Virtual(executor, "shutdown", "()V"));
}
