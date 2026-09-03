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

TEST_CASE("DVM-87 Locale publishes the API 19 ENGLISH singleton") {
    Dvm87Vm fixture;
    const auto locale =
        fixture.linker.ResolveDescriptor("Ljava/util/Locale;");
    const auto& locale_class = fixture.linker.Class(locale);
    CHECK(locale_class.access_flags == (kAccPublic | kAccFinal));
    CHECK(locale_class.direct_interfaces == std::vector<DexClassId>{
        fixture.linker.ResolveDescriptor("Ljava/lang/Cloneable;"),
        fixture.linker.ResolveDescriptor("Ljava/io/Serializable;")});

    const auto initialized = fixture.vm.EnsureClassInitialized(locale);
    Dvm87Vm::RequireOk(initialized);

    const auto english_field = fixture.linker.FindFieldRecursive(
        locale, "ENGLISH", "Ljava/util/Locale;");
    REQUIRE(english_field.has_value());
    const auto& linked = fixture.linker.Field(*english_field);
    CHECK(linked.access_flags == (kAccPublic | kAccStatic | kAccFinal));
    const auto english = VmObjectRef(
        fixture.linker.Class(linked.owner).static_storage[linked.slot]);
    REQUIRE(english.IsValid());
    CHECK(fixture.model.ObjectClass(english) == locale);

    Dvm87Vm::RequireOk(fixture.vm.EnsureClassInitialized(locale));
    CHECK(VmObjectRef(fixture.linker.Class(linked.owner)
                          .static_storage[linked.slot]) == english);
}

TEST_CASE("DVM-87 SimpleDateFormat shell preserves the API 19 hierarchy") {
    Dvm87Vm fixture;
    const auto format = fixture.linker.ResolveDescriptor("Ljava/text/Format;");
    const auto date_format =
        fixture.linker.ResolveDescriptor("Ljava/text/DateFormat;");
    const auto simple =
        fixture.linker.ResolveDescriptor("Ljava/text/SimpleDateFormat;");

    const auto& format_class = fixture.linker.Class(format);
    CHECK(format_class.access_flags == 0x0401U);
    CHECK(format_class.super.has_value());
    CHECK(*format_class.super ==
          fixture.linker.ResolveDescriptor("Ljava/lang/Object;"));
    CHECK(format_class.direct_interfaces == std::vector<DexClassId>{
        fixture.linker.ResolveDescriptor("Ljava/io/Serializable;"),
        fixture.linker.ResolveDescriptor("Ljava/lang/Cloneable;")});

    const auto& date_format_class = fixture.linker.Class(date_format);
    CHECK(date_format_class.access_flags == 0x0401U);
    CHECK(date_format_class.super.has_value());
    CHECK(*date_format_class.super == format);

    const auto& simple_class = fixture.linker.Class(simple);
    CHECK(simple_class.access_flags == 0x0001U);
    CHECK(simple_class.super.has_value());
    CHECK(*simple_class.super == date_format);
    CHECK(simple_class.own_direct_methods.size() == 1U);
    CHECK(simple_class.own_virtual_methods.empty());
}

TEST_CASE("DVM-87 SimpleDateFormat validates and stores an API 19 pattern") {
    Dvm87Vm fixture;
    const auto locale = fixture.Static(
        "Ljava/util/Locale;", "getDefault", "()Ljava/util/Locale;").value.ref;
    const auto descriptor =
        "(Ljava/lang/String;Ljava/util/Locale;)V";
    const auto construct = [&](const VmObjectRef object,
                               const VmObjectRef pattern,
                               const VmObjectRef requested_locale) {
        return fixture.Static(
            "Ljava/text/SimpleDateFormat;", "<init>", descriptor,
            {VmValue::Ref(object), VmValue::Ref(pattern),
             VmValue::Ref(requested_locale)});
    };
    const auto exception_is = [&](const VmCallOutcome& outcome,
                                  const std::string_view expected) {
        REQUIRE(outcome.exception.IsValid());
        CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
              expected);
    };

    const auto format = fixture.vm.NewIntrinsicInstance(
        "Ljava/text/SimpleDateFormat;");
    const auto pattern = fixture.vm.NewStringUtf8("yyyy-MM-dd 'at' HH:mm");
    Dvm87Vm::RequireOk(construct(format, pattern, locale));
    const auto pattern_field = fixture.linker.FindFieldRecursive(
        fixture.model.ObjectClass(format), "pattern", "Ljava/lang/String;");
    REQUIRE(pattern_field.has_value());
    const auto& linked_pattern = fixture.linker.Field(*pattern_field);
    CHECK(linked_pattern.access_flags == 0x0002U);
    const auto stored = VmObjectRef(
        fixture.model.InstanceSlots(format)[linked_pattern.slot].bits);
    CHECK(fixture.vm.StringUtf8(stored) == "yyyy-MM-dd 'at' HH:mm");

    const auto null_locale = fixture.vm.NewIntrinsicInstance(
        "Ljava/text/SimpleDateFormat;");
    exception_is(construct(null_locale, pattern, VmObjectRef{}),
                 "Ljava/lang/NullPointerException;");
    const auto null_pattern = fixture.vm.NewIntrinsicInstance(
        "Ljava/text/SimpleDateFormat;");
    exception_is(construct(null_pattern, VmObjectRef{}, locale),
                 "Ljava/lang/NullPointerException;");
    for (const auto invalid : {"yyyy-QQ", "yyyy-MM-dd 'open"}) {
        const auto object = fixture.vm.NewIntrinsicInstance(
            "Ljava/text/SimpleDateFormat;");
        exception_is(construct(object, fixture.vm.NewStringUtf8(invalid),
                               locale),
                     "Ljava/lang/IllegalArgumentException;");
    }
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
