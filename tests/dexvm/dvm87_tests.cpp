#include <doctest/doctest.h>

#include <cstdint>
#include <atomic>
#include <chrono>
#include <thread>
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/icu_formatter_runtime.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

std::vector<std::uint8_t> Dvm102BootDex() {
    const auto path = std::filesystem::path(OGPLAY_SOURCE_DIR) /
        "data/android/19/framework/bootdex.jar";
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), path.string());
    const std::vector<char> raw{std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>()};
    std::vector<std::byte> archive_bytes(raw.size());
    std::memcpy(archive_bytes.data(), raw.data(), raw.size());
    const auto archive = ogplay::loader::ParseApkArchive(archive_bytes);
    const auto dex = ogplay::loader::ReadApkEntry(
        archive_bytes, archive, "classes.dex");
    std::vector<std::uint8_t> result(dex.size());
    std::memcpy(result.data(), dex.data(), dex.size());
    return result;
}

struct Dvm87Vm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter vm;
    VmThreadRuntime threads;

    explicit Dvm87Vm(
        const InterpreterBackend backend = InterpreterBackend::switch_dispatch,
        const std::string& language = "zh",
        const std::string& iso3_language = "zho",
        const std::string& iso3_country = "CHN",
        const std::string& default_timezone = "GMT",
        const std::vector<IntrinsicClassDecl>& extras = {})
        : vm([this, &language, &iso3_language,
              &iso3_country, &default_timezone, &extras]() -> DexClassLinker& {
              CoreIntrinsicServices services;
              services.language = language;
              services.iso3_language = iso3_language;
              services.iso3_country = iso3_country;
              services.default_timezone = default_timezone;
              services.current_time_millis = [] { return 1704067200000LL; };
              linker.RegisterIntrinsics(CoreIntrinsicCatalog(services));
              linker.RegisterBootDex(Dvm102BootDex());
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
              linker.RegisterIntrinsics(extras);
              linker.Link();
              return linker;
          }(), model, nullptr, ledger, InterpreterConfig{.backend = backend}),
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
    const auto calendar_outcome = fixture.Static(
        "Ljava/util/Calendar;", "getInstance", "()Ljava/util/Calendar;");
    Dvm87Vm::RequireOk(calendar_outcome);
    const auto calendar = calendar_outcome.value.ref;
    CHECK(fixture.Virtual(calendar, "getTimeInMillis", "()J").value.AsLong() ==
          1704067200000LL);
    CHECK(fixture.Virtual(
        calendar, "get", "(I)I", {VmValue::Int(1)}).value.AsInt() == 2024);
    CHECK(fixture.Virtual(
        calendar, "get", "(I)I", {VmValue::Int(2)}).value.AsInt() == 0);

    const auto zone_outcome = fixture.Static(
        "Ljava/util/TimeZone;", "getTimeZone",
        "(Ljava/lang/String;)Ljava/util/TimeZone;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("GMT+08:00"))});
    Dvm87Vm::RequireOk(zone_outcome);
    const auto zone = zone_outcome.value.ref;
    CHECK(fixture.Virtual(zone, "getRawOffset", "()I").value.AsInt() ==
          8 * 60 * 60 * 1000);
}

TEST_CASE("DVM-87 Locale publishes ENGLISH and its injected default language") {
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
    const auto language_field = fixture.linker.FindFieldRecursive(
        locale, "languageCode", "Ljava/lang/String;");
    REQUIRE(language_field.has_value());
    CHECK(fixture.linker.Field(*language_field).access_flags ==
          (kAccPrivate | kAccTransient));
    const auto english_language = fixture.Virtual(
        english, "getLanguage", "()Ljava/lang/String;");
    Dvm87Vm::RequireOk(english_language);
    CHECK(fixture.vm.StringUtf8(english_language.value.ref) == "en");

    const auto default_locale = fixture.Static(
        "Ljava/util/Locale;", "getDefault", "()Ljava/util/Locale;");
    Dvm87Vm::RequireOk(default_locale);
    const auto default_language = fixture.Virtual(
        default_locale.value.ref, "getLanguage", "()Ljava/lang/String;");
    Dvm87Vm::RequireOk(default_language);
    CHECK(fixture.vm.StringUtf8(default_language.value.ref) == "zh");

    Dvm87Vm::RequireOk(fixture.vm.EnsureClassInitialized(locale));
    CHECK(VmObjectRef(fixture.linker.Class(linked.owner)
                          .static_storage[linked.slot]) == english);

    const auto locale_constant = [&](const std::string& name) {
        const auto field = fixture.linker.FindFieldRecursive(
            locale, name, "Ljava/util/Locale;");
        REQUIRE(field.has_value());
        const auto& linked_field = fixture.linker.Field(*field);
        return VmObjectRef(fixture.linker.Class(linked_field.owner)
                               .static_storage[linked_field.slot]);
    };
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(
        locale_constant("ROOT"), "toString", "()Ljava/lang/String;").value.ref)
          .empty());
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(
        locale_constant("US"), "toString", "()Ljava/lang/String;").value.ref) ==
          "en_US");
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(
        locale_constant("CHINESE"), "toString", "()Ljava/lang/String;").value.ref) ==
          "zh");

    Dvm87Vm english_vm(InterpreterBackend::switch_dispatch,
                       "en", "eng", "USA");
    const auto english_default = english_vm.Static(
        "Ljava/util/Locale;", "getDefault", "()Ljava/util/Locale;");
    Dvm87Vm::RequireOk(english_default);
    CHECK(english_vm.vm.StringUtf8(english_vm.Virtual(
        english_default.value.ref, "toString", "()Ljava/lang/String;").value.ref) ==
          "en_US");
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(
        default_locale.value.ref, "toString", "()Ljava/lang/String;").value.ref) ==
          "zh_CN");
}

TEST_CASE("DVM-102 SimpleDateFormat uses the API 19 BootDex hierarchy") {
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
    CHECK(simple_class.own_direct_methods.size() > 10U);
    CHECK(simple_class.own_virtual_methods.size() > 10U);
    const auto constructor = fixture.linker.FindDirectMethod(
        simple, "<init>", "(Ljava/lang/String;Ljava/util/Locale;)V");
    REQUIRE(constructor.has_value());
    CHECK(fixture.linker.Method(*constructor).kind == MethodKind::interpreted);
}

TEST_CASE("DVM-102 SimpleDateFormat initializes the API 19 object graph") {
  for (const auto backend : {InterpreterBackend::switch_dispatch,
                             InterpreterBackend::threaded}) {
    CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
    Dvm87Vm fixture(backend);
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
    const auto date = fixture.vm.NewIntrinsicInstance("Ljava/util/Date;");
    fixture.Construct(date, "Ljava/util/Date;", "(J)V",
                      {VmValue::Long(1704067200000LL)});
    const auto rendered = fixture.Virtual(
        format, "format", "(Ljava/util/Date;)Ljava/lang/String;",
        {VmValue::Ref(date)});
    const auto render_error = rendered.exception.IsValid()
        ? fixture.linker.Class(rendered.exception_class).descriptor + ": " +
              rendered.exception_message
        : std::string{};
    REQUIRE_MESSAGE(!rendered.exception.IsValid(), render_error);
    CHECK(fixture.vm.StringUtf8(rendered.value.ref) ==
          "2024-01-01 at 00:00");
    const auto parsed = fixture.Virtual(
        format, "parse", "(Ljava/lang/String;)Ljava/util/Date;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("2024-02-29 at 12:34"))});
    Dvm87Vm::RequireOk(parsed);
    CHECK(fixture.Virtual(parsed.value.ref, "getTime", "()J").value.AsLong() ==
          1709210040000LL);

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

    const auto cloned = fixture.Virtual(
        format, "clone", "()Ljava/lang/Object;");
    Dvm87Vm::RequireOk(cloned);
    Dvm87Vm::RequireOk(fixture.Virtual(
        cloned.value.ref, "applyPattern", "(Ljava/lang/String;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8("yyyy/MM/dd"))}));
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(
        cloned.value.ref, "format", "(Ljava/util/Date;)Ljava/lang/String;",
        {VmValue::Ref(date)}).value.ref) == "2024/01/01");
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(
        format, "format", "(Ljava/util/Date;)Ljava/lang/String;",
        {VmValue::Ref(date)}).value.ref) == "2024-01-01 at 00:00");

    const auto formatter_count = fixture.vm.IcuFormatters().Size();
    const auto transient_symbols = fixture.vm.NewIntrinsicInstance(
        "Ljava/text/DecimalFormatSymbols;");
    fixture.Construct(transient_symbols, "Ljava/text/DecimalFormatSymbols;",
                      "(Ljava/util/Locale;)V", {VmValue::Ref(locale)});
    const auto transient_formatter = fixture.vm.NewIntrinsicInstance(
        "Llibcore/icu/NativeDecimalFormat;");
    fixture.Construct(
        transient_formatter, "Llibcore/icu/NativeDecimalFormat;",
        "(Ljava/lang/String;Ljava/text/DecimalFormatSymbols;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8("#,##0")),
         VmValue::Ref(transient_symbols)});
    CHECK(fixture.vm.IcuFormatters().Size() == formatter_count + 1);
    const auto native_class = fixture.linker.ResolveDescriptor(
        "Llibcore/icu/NativeDecimalFormat;");
    CHECK(fixture.model.ObjectClass(transient_formatter) == native_class);
    const auto address = fixture.linker.FindFieldRecursive(
        native_class, "address", "J");
    REQUIRE(address.has_value());
    const auto& address_field = fixture.linker.Field(*address);
    const auto transient_slots = fixture.model.InstanceSlots(transient_formatter);
    CHECK(transient_slots[address_field.slot].tag == SlotTag::wide_lo);
    CHECK(transient_slots[address_field.slot + 1U].tag == SlotTag::wide_hi);
    const auto transient_token =
        static_cast<std::uint64_t>(transient_slots[address_field.slot].bits) |
        (static_cast<std::uint64_t>(
             transient_slots[address_field.slot + 1U].bits) << 32U);
    CHECK(fixture.vm.IcuFormatters().Contains(transient_token));
    const auto reachable = fixture.vm.MarkReachable();
    CHECK_FALSE(reachable.IsMarked(transient_formatter));
    static_cast<void>(fixture.vm.SweepGarbage(reachable));
    CHECK_FALSE(fixture.vm.IcuFormatters().Contains(transient_token));
    CHECK(fixture.vm.IcuFormatters().Size() <= formatter_count);
  }
}

TEST_CASE("DVM-102 date formatting covers constructors factories and zones") {
  for (const auto backend : {InterpreterBackend::switch_dispatch,
                             InterpreterBackend::threaded}) {
    CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
    Dvm87Vm fixture(backend);
    const auto default_locale = fixture.Static(
        "Ljava/util/Locale;", "getDefault", "()Ljava/util/Locale;");
    Dvm87Vm::RequireOk(default_locale);
    const auto pattern = fixture.vm.NewStringUtf8(
        "yyyy-MM-dd HH:mm:ss.SSS a MMMM EEEE z Z");
    const auto make_simple = [&] {
      return fixture.vm.NewIntrinsicInstance("Ljava/text/SimpleDateFormat;");
    };

    const auto empty = make_simple();
    fixture.Construct(empty, "Ljava/text/SimpleDateFormat;", "()V");
    const auto with_pattern = make_simple();
    fixture.Construct(with_pattern, "Ljava/text/SimpleDateFormat;",
                      "(Ljava/lang/String;)V", {VmValue::Ref(pattern)});
    const auto with_locale = make_simple();
    fixture.Construct(
        with_locale, "Ljava/text/SimpleDateFormat;",
        "(Ljava/lang/String;Ljava/util/Locale;)V",
        {VmValue::Ref(pattern), VmValue::Ref(default_locale.value.ref)});
    const auto symbols = fixture.vm.NewIntrinsicInstance(
        "Ljava/text/DateFormatSymbols;");
    fixture.Construct(
        symbols, "Ljava/text/DateFormatSymbols;", "(Ljava/util/Locale;)V",
        {VmValue::Ref(default_locale.value.ref)});
    const auto with_symbols = make_simple();
    fixture.Construct(
        with_symbols, "Ljava/text/SimpleDateFormat;",
        "(Ljava/lang/String;Ljava/text/DateFormatSymbols;)V",
        {VmValue::Ref(pattern), VmValue::Ref(symbols)});

    const auto date = fixture.vm.NewIntrinsicInstance("Ljava/util/Date;");
    fixture.Construct(date, "Ljava/util/Date;", "(J)V",
                      {VmValue::Long(0)});
    for (const auto [zone_id, expected] : {
             std::pair{"GMT+08:00", u"1970-01-01 08:00:00.000 上午 一月 星期四 GMT+08:00 +0800"},
             std::pair{"GMT-05:30", u"1969-12-31 18:30:00.000 下午 十二月 星期三 GMT-05:30 -0530"}}) {
      const auto zone = fixture.Static(
          "Ljava/util/TimeZone;", "getTimeZone",
          "(Ljava/lang/String;)Ljava/util/TimeZone;",
          {VmValue::Ref(fixture.vm.NewStringUtf8(zone_id))});
      Dvm87Vm::RequireOk(zone);
      Dvm87Vm::RequireOk(fixture.Virtual(
          with_locale, "setTimeZone", "(Ljava/util/TimeZone;)V",
          {VmValue::Ref(zone.value.ref)}));
      const auto formatted = fixture.Virtual(
          with_locale, "format", "(Ljava/util/Date;)Ljava/lang/String;",
          {VmValue::Ref(date)});
      Dvm87Vm::RequireOk(formatted);
      CHECK(fixture.model.StringValue(formatted.value.ref) == expected);
    }

    const auto gmt = fixture.Static(
        "Ljava/util/TimeZone;", "getTimeZone",
        "(Ljava/lang/String;)Ljava/util/TimeZone;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("GMT"))});
    Dvm87Vm::RequireOk(gmt);
    const auto boundary_formatter = make_simple();
    fixture.Construct(
        boundary_formatter, "Ljava/text/SimpleDateFormat;",
        "(Ljava/lang/String;Ljava/util/Locale;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8(
             "yyyy年MM月dd日 'epoch' HH:mm:ss.SSS")),
         VmValue::Ref(default_locale.value.ref)});
    Dvm87Vm::RequireOk(fixture.Virtual(
        boundary_formatter, "setTimeZone", "(Ljava/util/TimeZone;)V",
        {VmValue::Ref(gmt.value.ref)}));
    const auto before_epoch = fixture.vm.NewIntrinsicInstance("Ljava/util/Date;");
    fixture.Construct(before_epoch, "Ljava/util/Date;", "(J)V",
                      {VmValue::Long(-1)});
    CHECK(fixture.model.StringValue(fixture.Virtual(
        boundary_formatter, "format", "(Ljava/util/Date;)Ljava/lang/String;",
        {VmValue::Ref(before_epoch)}).value.ref) ==
          u"1969年12月31日 epoch 23:59:59.999");

    const auto full_year = make_simple();
    fixture.Construct(
        full_year, "Ljava/text/SimpleDateFormat;",
        "(Ljava/lang/String;Ljava/util/Locale;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8("yyyy-MM-dd")),
         VmValue::Ref(default_locale.value.ref)});
    const auto two_digit = make_simple();
    fixture.Construct(
        two_digit, "Ljava/text/SimpleDateFormat;",
        "(Ljava/lang/String;Ljava/util/Locale;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8("yy-MM-dd")),
         VmValue::Ref(default_locale.value.ref)});
    for (const auto [text, expected] : {
             std::pair{"43-12-01", "2043-12-01"},
             std::pair{"44-12-01", "1944-12-01"}}) {
      const auto parsed = fixture.Virtual(
          two_digit, "parse", "(Ljava/lang/String;)Ljava/util/Date;",
          {VmValue::Ref(fixture.vm.NewStringUtf8(text))});
      Dvm87Vm::RequireOk(parsed);
      CHECK(fixture.vm.StringUtf8(fixture.Virtual(
          full_year, "format", "(Ljava/util/Date;)Ljava/lang/String;",
          {VmValue::Ref(parsed.value.ref)}).value.ref) == expected);
    }

    Dvm87Vm::RequireOk(fixture.Virtual(
        full_year, "setLenient", "(Z)V", {VmValue::Int(1)}));
    const auto lenient = fixture.Virtual(
        full_year, "parse", "(Ljava/lang/String;)Ljava/util/Date;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("2023-02-29"))});
    Dvm87Vm::RequireOk(lenient);
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(
        full_year, "format", "(Ljava/util/Date;)Ljava/lang/String;",
        {VmValue::Ref(lenient.value.ref)}).value.ref) == "2023-03-01");

    const auto injected_calendar = fixture.Static(
        "Ljava/util/Calendar;", "getInstance", "()Ljava/util/Calendar;");
    Dvm87Vm::RequireOk(injected_calendar);
    Dvm87Vm::RequireOk(fixture.Virtual(
        injected_calendar.value.ref, "setTimeZone", "(Ljava/util/TimeZone;)V",
        {VmValue::Ref(gmt.value.ref)}));
    Dvm87Vm::RequireOk(fixture.Virtual(
        full_year, "setCalendar", "(Ljava/util/Calendar;)V",
        {VmValue::Ref(injected_calendar.value.ref)}));
    CHECK(fixture.Virtual(
        full_year, "getCalendar", "()Ljava/util/Calendar;").value.ref ==
          injected_calendar.value.ref);
    const auto injected_number = fixture.vm.NewIntrinsicInstance(
        "Ljava/text/DecimalFormat;");
    fixture.Construct(
        injected_number, "Ljava/text/DecimalFormat;", "(Ljava/lang/String;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8("0000"))});
    Dvm87Vm::RequireOk(fixture.Virtual(
        full_year, "setNumberFormat", "(Ljava/text/NumberFormat;)V",
        {VmValue::Ref(injected_number)}));
    CHECK(fixture.Virtual(
        full_year, "getNumberFormat", "()Ljava/text/NumberFormat;").value.ref ==
          injected_number);

    Dvm87Vm::RequireOk(fixture.Virtual(
        injected_calendar.value.ref, "setTimeInMillis", "(J)V",
        {VmValue::Long(1675123200000LL)}));
    Dvm87Vm::RequireOk(fixture.Virtual(
        injected_calendar.value.ref, "add", "(II)V",
        {VmValue::Int(2), VmValue::Int(1)}));
    const auto month_end = fixture.Virtual(
        injected_calendar.value.ref, "getTime", "()Ljava/util/Date;");
    Dvm87Vm::RequireOk(month_end);
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(
        full_year, "format", "(Ljava/util/Date;)Ljava/lang/String;",
        {VmValue::Ref(month_end.value.ref)}).value.ref) == "2023-02-28");
    Dvm87Vm::RequireOk(fixture.Virtual(
        injected_calendar.value.ref, "setTimeInMillis", "(J)V",
        {VmValue::Long(1709164800000LL)}));
    Dvm87Vm::RequireOk(fixture.Virtual(
        injected_calendar.value.ref, "add", "(II)V",
        {VmValue::Int(1), VmValue::Int(-1)}));
    const auto leap_year = fixture.Virtual(
        injected_calendar.value.ref, "getTime", "()Ljava/util/Date;");
    Dvm87Vm::RequireOk(leap_year);
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(
        full_year, "format", "(Ljava/util/Date;)Ljava/lang/String;",
        {VmValue::Ref(leap_year.value.ref)}).value.ref) == "2023-02-28");

    for (const auto factory : {
             std::pair{"getDateInstance", "(ILjava/util/Locale;)Ljava/text/DateFormat;"},
             std::pair{"getTimeInstance", "(ILjava/util/Locale;)Ljava/text/DateFormat;"}}) {
      const auto created = fixture.Static(
          "Ljava/text/DateFormat;", factory.first, factory.second,
          {VmValue::Int(2), VmValue::Ref(default_locale.value.ref)});
      Dvm87Vm::RequireOk(created);
      CHECK(created.value.ref.IsValid());
    }
    const auto combined = fixture.Static(
        "Ljava/text/DateFormat;", "getDateTimeInstance",
        "(IILjava/util/Locale;)Ljava/text/DateFormat;",
        {VmValue::Int(3), VmValue::Int(3),
         VmValue::Ref(default_locale.value.ref)});
    Dvm87Vm::RequireOk(combined);
    CHECK(combined.value.ref.IsValid());
    const auto invalid_style = fixture.Static(
        "Ljava/text/DateFormat;", "getDateInstance",
        "(ILjava/util/Locale;)Ljava/text/DateFormat;",
        {VmValue::Int(99), VmValue::Ref(default_locale.value.ref)});
    REQUIRE(invalid_style.exception.IsValid());
    CHECK(fixture.linker.Class(invalid_style.exception_class).descriptor ==
          "Ljava/lang/IllegalArgumentException;");

    const auto invalid_named = fixture.Static(
        "Ljava/util/TimeZone;", "getTimeZone",
        "(Ljava/lang/String;)Ljava/util/TimeZone;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("NotAZone"))});
    Dvm87Vm::RequireOk(invalid_named);
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(
        invalid_named.value.ref, "getID", "()Ljava/lang/String;").value.ref) ==
          "GMT");
    const auto missing_database = fixture.Static(
        "Ljava/util/TimeZone;", "getTimeZone",
        "(Ljava/lang/String;)Ljava/util/TimeZone;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("America/New_York"))});
    REQUIRE(missing_database.exception.IsValid());
    CHECK(fixture.linker.Class(missing_database.exception_class).descriptor ==
          "Ljava/lang/UnsupportedOperationException;");

    const auto parser = make_simple();
    fixture.Construct(
        parser, "Ljava/text/SimpleDateFormat;",
        "(Ljava/lang/String;Ljava/util/Locale;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8("yyyy-MM-dd")),
         VmValue::Ref(default_locale.value.ref)});
    Dvm87Vm::RequireOk(fixture.Virtual(
        parser, "setLenient", "(Z)V", {VmValue::Int(0)}));
    const auto position = fixture.vm.NewIntrinsicInstance(
        "Ljava/text/ParsePosition;");
    fixture.Construct(position, "Ljava/text/ParsePosition;", "(I)V",
                      {VmValue::Int(7)});
    const auto partial = fixture.Virtual(
        parser, "parse",
        "(Ljava/lang/String;Ljava/text/ParsePosition;)Ljava/util/Date;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("prefix 2024-02-29 tail")),
         VmValue::Ref(position)});
    Dvm87Vm::RequireOk(partial);
    CHECK(partial.value.ref.IsValid());
    CHECK(fixture.Virtual(position, "getIndex", "()I").value.AsInt() == 17);
    const auto invalid_date = fixture.Virtual(
        parser, "parse", "(Ljava/lang/String;)Ljava/util/Date;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("2023-02-29"))});
    REQUIRE(invalid_date.exception.IsValid());
    CHECK(fixture.linker.Class(invalid_date.exception_class).descriptor ==
          "Ljava/text/ParseException;");

    const auto field = fixture.vm.NewIntrinsicInstance(
        "Ljava/text/FieldPosition;");
    fixture.Construct(field, "Ljava/text/FieldPosition;", "(I)V",
                      {VmValue::Int(1)});
    const auto buffer = fixture.vm.NewIntrinsicInstance(
        "Ljava/lang/StringBuffer;");
    fixture.Construct(buffer, "Ljava/lang/StringBuffer;", "()V");
    const auto field_rendered = fixture.Virtual(
        parser, "format",
        "(Ljava/util/Date;Ljava/lang/StringBuffer;Ljava/text/FieldPosition;)Ljava/lang/StringBuffer;",
        {VmValue::Ref(date), VmValue::Ref(buffer), VmValue::Ref(field)});
    Dvm87Vm::RequireOk(field_rendered);
    CHECK(fixture.Virtual(field, "getBeginIndex", "()I").value.AsInt() == 0);
    CHECK(fixture.Virtual(field, "getEndIndex", "()I").value.AsInt() == 4);
    const auto attributed = fixture.Virtual(
        parser, "formatToCharacterIterator",
        "(Ljava/lang/Object;)Ljava/text/AttributedCharacterIterator;",
        {VmValue::Ref(date)});
    Dvm87Vm::RequireOk(attributed);
    CHECK(attributed.value.ref.IsValid());
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

// Independent DVM-102 acceptance regressions (pinned API19 behavior).
TEST_CASE("DVM-102 regression default timezone respects Java setDefault") {
 for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
  Dvm87Vm f(backend);
  auto zone=f.Static("Ljava/util/TimeZone;", "getTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;", {VmValue::Ref(f.vm.NewStringUtf8("GMT+08:00"))});
  Dvm87Vm::RequireOk(zone);
  Dvm87Vm::RequireOk(f.Static("Ljava/util/TimeZone;", "setDefault", "(Ljava/util/TimeZone;)V", {zone.value}));
  auto actual=f.Static("Ljava/util/TimeZone;", "getDefault", "()Ljava/util/TimeZone;");
  Dvm87Vm::RequireOk(actual);
  CHECK(f.Virtual(actual.value.ref, "getRawOffset", "()I").value.AsInt()==28800000);
 }
}
TEST_CASE("DVM-102 regression comma literal survives disabled grouping in date parser") {
 for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
  Dvm87Vm f(backend);
  auto fmt=f.vm.NewIntrinsicInstance("Ljava/text/SimpleDateFormat;");
  f.Construct(fmt, "Ljava/text/SimpleDateFormat;", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("yyyy,MM,dd"))});
  auto parsed=f.Virtual(fmt,"parse","(Ljava/lang/String;)Ljava/util/Date;",{VmValue::Ref(f.vm.NewStringUtf8("2024,01,02"))});
  CHECK_MESSAGE(!parsed.exception.IsValid(), parsed.exception_message);
  if (!parsed.exception.IsValid()) CHECK(f.Virtual(parsed.value.ref,"getTime","()J").value.AsLong()==1704153600000LL);
 }
}
TEST_CASE("DVM-102 regression integer decimal pattern and attributes take effect") {
 const auto narrow=[](const std::u16string& s) { return std::string(s.begin(),s.end()); };
 IcuFormatterRuntime r;
 auto fixed=r.Open(u"0.00",{});
 CHECK(narrow(r.FormatLong(fixed,12).text)=="12.00");
 auto pct=r.Open(u"0%",{});
 CHECK(narrow(r.FormatLong(pct,12).text)=="1200%");
 auto negative=r.Open(u"0;(0)",{});
 CHECK(narrow(r.FormatLong(negative,-12).text)=="(12)");
 auto clipped=r.Open(u"0",{});
 r.SetAttribute(clipped,3,2);
 CHECK(narrow(r.FormatLong(clipped,1234).text)=="34");
 auto no_group=r.Open(u"0",{});
 r.SetAttribute(no_group,1,0);
 auto parsed=r.ParseInteger(no_group,u"12,34",0);
 REQUIRE(parsed.has_value());
 CHECK(parsed->value==12);
 CHECK(parsed->end==2);
}
TEST_CASE("DVM-102 regression Locale constructor normalizes supported en_US") {
 Dvm87Vm f;
 auto locale=f.vm.NewIntrinsicInstance("Ljava/util/Locale;");
 f.Construct(locale,"Ljava/util/Locale;","(Ljava/lang/String;Ljava/lang/String;)V",{VmValue::Ref(f.vm.NewStringUtf8("EN")),VmValue::Ref(f.vm.NewStringUtf8("us"))});
 CHECK(f.vm.StringUtf8(f.Virtual(locale,"toString","()Ljava/lang/String;").value.ref)=="en_US");
 auto fmt=f.vm.NewIntrinsicInstance("Ljava/text/SimpleDateFormat;");
 auto result=f.Static("Ljava/text/SimpleDateFormat;","<init>","(Ljava/lang/String;Ljava/util/Locale;)V",{VmValue::Ref(fmt),VmValue::Ref(f.vm.NewStringUtf8("yyyy")),VmValue::Ref(locale)});
 CHECK_MESSAGE(!result.exception.IsValid(),result.exception_message);
}
TEST_CASE("DVM-102 regression Regex groupCount does not depend on match success") {
 Dvm87Vm f;
 auto p=f.Static("Ljava/util/regex/Pattern;","compile","(Ljava/lang/String;)Ljava/util/regex/Pattern;",{VmValue::Ref(f.vm.NewStringUtf8("(a)(b)?"))});
 Dvm87Vm::RequireOk(p);
 auto matcher=f.Virtual(p.value.ref,"matcher","(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;",{VmValue::Ref(f.vm.NewStringUtf8("xx"))});
 Dvm87Vm::RequireOk(matcher);
 auto count=f.Virtual(matcher.value.ref,"groupCount","()I");
 CHECK_MESSAGE(!count.exception.IsValid(),count.exception_message);
 if (!count.exception.IsValid()) CHECK(count.value.AsInt()==2);
}
TEST_CASE("DVM-102 regression narrow English date symbols are narrow") {
 Dvm87Vm f(InterpreterBackend::switch_dispatch,"en","eng","USA");
 auto fmt=f.vm.NewIntrinsicInstance("Ljava/text/SimpleDateFormat;");
 f.Construct(fmt,"Ljava/text/SimpleDateFormat;","(Ljava/lang/String;)V",{VmValue::Ref(f.vm.NewStringUtf8("MMMMM EEEEE"))});
 auto date=f.vm.NewIntrinsicInstance("Ljava/util/Date;");
 f.Construct(date,"Ljava/util/Date;","(J)V",{VmValue::Long(0)});
 auto value=f.Virtual(fmt,"format","(Ljava/util/Date;)Ljava/lang/String;",{VmValue::Ref(date)});
 Dvm87Vm::RequireOk(value);
 CHECK(f.vm.StringUtf8(value.value.ref)=="J T");
}

TEST_CASE("DVM-102 regression BootDex static values follow DEX field order") {
  Dvm87Vm f;
  const auto owner = f.linker.ResolveDescriptor("Ljava/util/TimeZone;");
  Dvm87Vm::RequireOk(f.vm.EnsureClassInitialized(owner));
  for (const auto& [name, expected] : std::array{std::pair{"LONG", 1U}, std::pair{"SHORT", 0U}}) {
    const auto field = f.linker.FindFieldRecursive(owner, name, "I");
    REQUIRE(field.has_value());
    CHECK(f.linker.Class(owner).static_storage[f.linker.Field(*field).slot] == expected);
  }
  const auto field = f.linker.FindFieldRecursive(owner, "defaultTimeZone", "Ljava/util/TimeZone;");
  REQUIRE(field.has_value());
  CHECK(f.linker.Class(owner).static_storage[f.linker.Field(*field).slot] == 0U);
}

TEST_CASE("DVM-102 regression default timezone cloning reset and VM isolation") {
  for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    Dvm87Vm f(backend);
    const auto get = [&] {
      auto result = f.Static("Ljava/util/TimeZone;", "getDefault", "()Ljava/util/TimeZone;");
      Dvm87Vm::RequireOk(result);
      return result.value.ref;
    };
    auto zone = get();
    Dvm87Vm::RequireOk(f.Virtual(zone, "setRawOffset", "(I)V", {VmValue::Int(28800000)}));
    CHECK(f.Virtual(get(), "getRawOffset", "()I").value.AsInt() == 0);
    Dvm87Vm::RequireOk(f.Static("Ljava/util/TimeZone;", "setDefault", "(Ljava/util/TimeZone;)V", {VmValue::Ref(zone)}));
    Dvm87Vm::RequireOk(f.Virtual(zone, "setRawOffset", "(I)V", {VmValue::Int(0)}));
    CHECK(f.Virtual(get(), "getRawOffset", "()I").value.AsInt() == 28800000);
    Dvm87Vm separate(backend);
    auto other = separate.Static("Ljava/util/TimeZone;", "getDefault", "()Ljava/util/TimeZone;");
    Dvm87Vm::RequireOk(other);
    CHECK(separate.Virtual(other.value.ref, "getRawOffset", "()I").value.AsInt() == 0);
    Dvm87Vm::RequireOk(f.Static("Ljava/util/TimeZone;", "setDefault", "(Ljava/util/TimeZone;)V", {VmValue::Ref(VmObjectRef{})}));
    CHECK(f.Virtual(get(), "getRawOffset", "()I").value.AsInt() == 0);
    const auto named = f.Static("Ljava/util/TimeZone;", "getTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;",
        {VmValue::Ref(f.vm.NewStringUtf8("CET"))});
    REQUIRE(named.exception.IsValid());
    CHECK(f.linker.Class(named.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
  }
}

TEST_CASE("DVM-102 regression native parse preserves API19 position semantics") {
  for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    Dvm87Vm f(backend);
    const auto format = f.vm.NewIntrinsicInstance("Ljava/text/DecimalFormat;");
    f.Construct(format, "Ljava/text/DecimalFormat;", "(Ljava/lang/String;)V",
        {VmValue::Ref(f.vm.NewStringUtf8("0"))});
    const auto position = f.vm.NewIntrinsicInstance("Ljava/text/ParsePosition;");
    f.Construct(position, "Ljava/text/ParsePosition;", "(I)V", {VmValue::Int(0)});
    Dvm87Vm::RequireOk(f.Virtual(position, "setErrorIndex", "(I)V", {VmValue::Int(7)}));
    auto parsed = f.Virtual(format, "parse", "(Ljava/lang/String;Ljava/text/ParsePosition;)Ljava/lang/Number;",
        {VmValue::Ref(f.vm.NewStringUtf8("12")), VmValue::Ref(position)});
    Dvm87Vm::RequireOk(parsed);
    REQUIRE(parsed.value.ref.IsValid());
    CHECK(f.Virtual(position, "getIndex", "()I").value.AsInt() == 2);
    CHECK(f.Virtual(position, "getErrorIndex", "()I").value.AsInt() == 7);
    Dvm87Vm::RequireOk(f.Virtual(position, "setIndex", "(I)V", {VmValue::Int(-1)}));
    parsed = f.Virtual(format, "parse", "(Ljava/lang/String;Ljava/text/ParsePosition;)Ljava/lang/Number;",
        {VmValue::Ref(f.vm.NewStringUtf8("12")), VmValue::Ref(position)});
    Dvm87Vm::RequireOk(parsed);
    CHECK_FALSE(parsed.value.ref.IsValid());
    CHECK(f.Virtual(position, "getErrorIndex", "()I").value.AsInt() == 7);
    const auto invalid = f.Virtual(format, "applyPattern", "(Ljava/lang/String;)V",
        {VmValue::Ref(f.vm.NewStringUtf8("0.0.0"))});
    REQUIRE(invalid.exception.IsValid());
    CHECK(f.linker.Class(invalid.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
  }
}

TEST_CASE("DVM-102 regression configured timezone is restored after Java reset") {
  for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    Dvm87Vm f(backend, "en", "eng", "USA", "GMT-03:30");
    for (int iteration = 0; iteration < 2; ++iteration) {
      const auto zone = f.Static("Ljava/util/TimeZone;", "getDefault", "()Ljava/util/TimeZone;");
      Dvm87Vm::RequireOk(zone);
      CHECK(f.Virtual(zone.value.ref, "getRawOffset", "()I").value.AsInt() == -12600000);
      Dvm87Vm::RequireOk(f.Static("Ljava/util/TimeZone;", "setDefault", "(Ljava/util/TimeZone;)V",
          {VmValue::Ref(VmObjectRef{})}));
    }
  }
}

TEST_CASE("DVM-103 BootDex collection implementations own their methods and storage") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        for (const auto* name : {"ArrayList", "LinkedList", "Vector", "Stack", "ArrayDeque",
                                "HashSet", "LinkedHashSet", "TreeSet", "PriorityQueue",
                                "concurrent/CopyOnWriteArrayList", "concurrent/CopyOnWriteArraySet",
                                "concurrent/ConcurrentLinkedQueue", "concurrent/ConcurrentLinkedDeque",
                                "concurrent/LinkedBlockingQueue", "concurrent/LinkedBlockingDeque",
                                "concurrent/PriorityBlockingQueue", "concurrent/ConcurrentSkipListSet"}) {
            CAPTURE(name);
            const auto descriptor = std::string("Ljava/util/") + name + ";";
            const auto type = f.linker.ResolveDescriptor(descriptor);
            CHECK(f.linker.Class(type).is_boot_dex);
            for (const auto method : f.linker.Class(type).own_virtual_methods)
                CHECK(f.linker.Method(method).kind != MethodKind::intrinsic);
            const auto object = f.vm.NewIntrinsicInstance(descriptor);
            f.Construct(object, descriptor, "()V");
            const auto a = f.vm.NewStringUtf8("b");
            const auto b = f.vm.NewStringUtf8("a");
            f.RequireOk(f.Virtual(object, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(a)}));
            f.RequireOk(f.Virtual(object, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(b)}));
            auto size = f.Virtual(object, "size", "()I"); f.RequireOk(size); CHECK(size.value.AsInt() == 2);
            auto it = f.Virtual(object, "iterator", "()Ljava/util/Iterator;"); f.RequireOk(it);
            auto next = f.Virtual(it.value.ref, "next", "()Ljava/lang/Object;"); f.RequireOk(next);
            CHECK((next.value.ref == a || next.value.ref == b));
            f.RequireOk(f.Virtual(object, "clear", "()V"));
            size = f.Virtual(object, "size", "()I"); f.RequireOk(size); CHECK(size.value.AsInt() == 0);
        }
        for (const auto* name : {"HashMap", "LinkedHashMap", "Hashtable", "TreeMap", "IdentityHashMap",
                                "WeakHashMap", "Properties", "concurrent/ConcurrentHashMap",
                                "concurrent/ConcurrentSkipListMap"}) {
            CAPTURE(name);
            const auto descriptor = std::string("Ljava/util/") + name + ";";
            const auto object = f.vm.NewIntrinsicInstance(descriptor);
            f.Construct(object, descriptor, "()V");
            const auto key = f.vm.NewStringUtf8("key");
            const auto value = f.vm.NewStringUtf8("value");
            f.RequireOk(f.Virtual(object, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                 {VmValue::Ref(key), VmValue::Ref(value)}));
            auto got = f.Virtual(object, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {VmValue::Ref(key)});
            f.RequireOk(got); CHECK(got.value.ref == value);
            const auto view = f.Virtual(object, "entrySet", "()Ljava/util/Set;"); f.RequireOk(view);
            const auto it = f.Virtual(view.value.ref, "iterator", "()Ljava/util/Iterator;"); f.RequireOk(it);
            const auto entry = f.Virtual(it.value.ref, "next", "()Ljava/lang/Object;"); f.RequireOk(entry);
            got = f.Virtual(entry.value.ref, "getKey", "()Ljava/lang/Object;"); f.RequireOk(got); CHECK(got.value.ref == key);
            f.RequireOk(f.Virtual(object, "clear", "()V"));
            got = f.Virtual(view.value.ref, "size", "()I"); f.RequireOk(got); CHECK(got.value.AsInt() == 0);
        }
    }
}

TEST_CASE("DVM-103 BootDex weak keys clear and enqueue while live list elements survive GC") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        const auto map = f.vm.NewIntrinsicInstance("Ljava/util/WeakHashMap;");
        f.Construct(map, "Ljava/util/WeakHashMap;", "()V");
        const auto list = f.vm.NewIntrinsicInstance("Ljava/util/ArrayList;");
        f.Construct(list, "Ljava/util/ArrayList;", "()V");
        const auto key = f.vm.NewIntrinsicInstance("Ljava/lang/Object;");
        const auto value = f.vm.NewIntrinsicInstance("Ljava/lang/Object;");
        const auto queue = f.vm.NewIntrinsicInstance("Ljava/lang/ref/ReferenceQueue;");
        f.Construct(queue, "Ljava/lang/ref/ReferenceQueue;", "()V");
        const auto weak = f.vm.NewIntrinsicInstance("Ljava/lang/ref/WeakReference;");
        f.Construct(weak, "Ljava/lang/ref/WeakReference;",
            "(Ljava/lang/Object;Ljava/lang/ref/ReferenceQueue;)V", {VmValue::Ref(key), VmValue::Ref(queue)});
        f.RequireOk(f.Virtual(map, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                             {VmValue::Ref(key), VmValue::Ref(value)}));
        f.RequireOk(f.Virtual(list, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(value)}));
        const std::array roots{map, list, weak, queue};
        const auto protect = f.vm.ProtectReferences(roots);
        static_cast<void>(f.vm.CollectGarbage());
        CHECK_FALSE(f.model.IsValidRef(key));
        CHECK(f.model.IsValidRef(value));
        const auto cleared = f.Virtual(weak, "get", "()Ljava/lang/Object;");
        f.RequireOk(cleared); CHECK_FALSE(cleared.value.ref.IsValid());
        const auto polled = f.Virtual(queue, "poll", "()Ljava/lang/ref/Reference;");
        f.RequireOk(polled); CHECK(polled.value.ref == weak);
        const auto reenqueue = f.Virtual(weak, "enqueue", "()Z");
        f.RequireOk(reenqueue); CHECK(reenqueue.value.AsInt() == 0);
        auto size = f.Virtual(map, "size", "()I"); f.RequireOk(size); CHECK(size.value.AsInt() == 0);
        auto clone = f.Virtual(list, "clone", "()Ljava/lang/Object;"); f.RequireOk(clone);
        f.RequireOk(f.Virtual(list, "clear", "()V"));
        size = f.Virtual(clone.value.ref, "size", "()I"); f.RequireOk(size); CHECK(size.value.AsInt() == 1);
        const auto got = f.Virtual(clone.value.ref, "get", "(I)Ljava/lang/Object;", {VmValue::Int(0)});
        f.RequireOk(got); CHECK(got.value.ref == value);
    }
}

TEST_CASE("DVM-103 Externalizable invokes public constructor and callbacks with shared handles") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        bool public_constructor = true;
        bool fail_write = false;
        bool fail_read = false;
        SUBCASE("round trip") {}
        SUBCASE("private constructor is rejected") { public_constructor = false; }
        SUBCASE("write callback preserves throwable identity") { fail_write = true; }
        SUBCASE("read callback preserves throwable identity") { fail_read = true; }
        auto expected_failure = std::make_shared<VmObjectRef>();
        auto builder = IntrinsicClassBuilder::Class("Ltest/External;", "Ljava/lang/Object;", {"Ljava/io/Externalizable;"});
        builder.ConstantInt("serialVersionUID", "J", 123, kAccPrivate);
        const auto value = builder.BoundInstanceField("value", "I");
        const auto self = builder.BoundInstanceField("self", "Ljava/lang/Object;");
        const auto constructed = builder.BoundInstanceField("constructed", "Z");
        builder.Constructor("()V", [constructed](IntrinsicContext& c) {
            IntrinsicCall(c).SetInt(constructed, 1); return VmValue::Void();
        }, public_constructor ? kAccPublic : kAccPrivate);
        const auto invoke = [](IntrinsicContext& c, VmObjectRef receiver, const char* name,
                               const char* signature, std::vector<VmValue> args = {}) {
            const auto type = c.vm.Model().ObjectClass(receiver);
            const auto slot = c.vm.Linker().FindVtableIndex(type, name, signature);
            REQUIRE(slot.has_value()); args.insert(args.begin(), VmValue::Ref(receiver));
            const auto result = c.vm.Call(c.vm.Linker().Class(type).vtable[*slot], args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        builder.VirtualMethod("writeExternal", "(Ljava/io/ObjectOutput;)V", [value, invoke, fail_write, expected_failure](IntrinsicContext& c) {
            if (fail_write) {
                *expected_failure = c.vm.NewIntrinsicInstance("Ljava/io/IOException;");
                c.vm.SetPendingException(*expected_failure);
                return VmValue::Void();
            }
            const auto out = c.arguments[0].ref;
            invoke(c, out, "writeInt", "(I)V", {VmValue::Int(IntrinsicCall(c).GetInt(value))});
            invoke(c, out, "writeObject", "(Ljava/lang/Object;)V", {VmValue::Ref(c.receiver)});
            // The reader may leave trailing primitive and object data unconsumed.
            invoke(c, out, "writeInt", "(I)V", {VmValue::Int(99)});
            invoke(c, out, "writeObject", "(Ljava/lang/Object;)V", {VmValue::Ref(c.receiver)});
            return VmValue::Void();
        });
        builder.VirtualMethod("readExternal", "(Ljava/io/ObjectInput;)V", [value, self, invoke, fail_read, expected_failure](IntrinsicContext& c) {
            if (fail_read) {
                *expected_failure = c.vm.NewIntrinsicInstance("Ljava/io/IOException;");
                c.vm.SetPendingException(*expected_failure);
                return VmValue::Void();
            }
            const auto in = c.arguments[0].ref;
            IntrinsicCall(c).SetInt(value, invoke(c, in, "readInt", "()I").AsInt());
            IntrinsicCall(c).SetRef(self, invoke(c, in, "readObject", "()Ljava/lang/Object;").ref);
            return VmValue::Void();
        });
        std::vector<IntrinsicClassDecl> extras{std::move(builder).Build()};
        Dvm87Vm f(backend, "en", "eng", "USA", "GMT", extras);
        const auto object = f.vm.NewIntrinsicInstance("Ltest/External;");
        const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), "value", "I"); REQUIRE(field);
        f.model.InstanceSlots(object)[f.linker.Field(*field).slot] = {42, SlotTag::cat1};
        const auto bytes = f.vm.NewIntrinsicInstance("Ljava/io/ByteArrayOutputStream;");
        f.Construct(bytes, "Ljava/io/ByteArrayOutputStream;", "()V");
        const auto out = f.vm.NewIntrinsicInstance("Ljava/io/ObjectOutputStream;");
        f.Construct(out, "Ljava/io/ObjectOutputStream;", "(Ljava/io/OutputStream;)V", {VmValue::Ref(bytes)});
        const auto written = f.Virtual(out, "writeObject", "(Ljava/lang/Object;)V", {VmValue::Ref(object)});
        if (fail_write) { CHECK(written.exception == *expected_failure); continue; }
        f.RequireOk(written);
        f.RequireOk(f.Virtual(out, "writeObject", "(Ljava/lang/Object;)V", {VmValue::Ref(object)}));
        f.RequireOk(f.Virtual(out, "flush", "()V"));
        const auto encoded = f.vm.IO().Output(out).bytes;
        const auto raw = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"),
            JniPrimitiveKind::byte, static_cast<JniSize>(encoded.size()));
        f.model.WriteByteRegion(raw, 0, encoded);
        const auto input = f.vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
        f.Construct(input, "Ljava/io/ByteArrayInputStream;", "([B)V", {VmValue::Ref(raw)});
        const auto in = f.vm.NewIntrinsicInstance("Ljava/io/ObjectInputStream;");
        f.Construct(in, "Ljava/io/ObjectInputStream;", "(Ljava/io/InputStream;)V", {VmValue::Ref(input)});
        const auto first = f.Virtual(in, "readObject", "()Ljava/lang/Object;");
        if (!public_constructor) {
            REQUIRE(first.exception.IsValid());
            CHECK(f.linker.Class(first.exception_class).descriptor == "Ljava/io/InvalidClassException;");
            continue;
        }
        if (fail_read) { CHECK(first.exception == *expected_failure); continue; }
        f.RequireOk(first);
        const auto second = f.Virtual(in, "readObject", "()Ljava/lang/Object;"); f.RequireOk(second);
        CHECK(first.value.ref == second.value.ref);
        CHECK(first.value.ref != object);
        const auto type = f.model.ObjectClass(first.value.ref);
        const auto slots = f.model.InstanceSlots(first.value.ref);
        CHECK(slots[f.linker.Field(*field).slot].bits == 42);
        const auto self_field = f.linker.FindFieldRecursive(type, "self", "Ljava/lang/Object;"); REQUIRE(self_field);
        CHECK(slots[f.linker.Field(*self_field).slot].bits == first.value.ref.Value());
        const auto ctor_field = f.linker.FindFieldRecursive(type, "constructed", "Z"); REQUIRE(ctor_field);
        CHECK(slots[f.linker.Field(*ctor_field).slot].bits == 1);
    }
}

TEST_CASE("DVM-103 all BootDex classes link and collection methods have no intrinsic overlay") {
    Dvm87Vm f;
    const auto all = f.linker.AllClasses();
    std::size_t count{};
    for (const auto type : all) {
        if (!f.linker.Class(type).is_boot_dex) continue;
        const auto descriptor = f.linker.Class(type).descriptor;
        CAPTURE(std::string(descriptor));
        f.linker.EnsureClassLinked(type);
        ++count;
        if (descriptor.starts_with("Ljavax/crypto/") ||
            descriptor.starts_with("Lcom/android/org/conscrypt/OpenSSLCipher$") ||
            descriptor == "Lcom/android/org/conscrypt/OpenSSLCipher;" ||
            descriptor.starts_with("Ljava/io/") || descriptor.starts_with("Ljava/beans/") ||
            descriptor.starts_with("Ljava/util/concurrent/atomic/") ||
            descriptor.starts_with("Ljava/util/concurrent/CountDownLatch") ||
            descriptor.starts_with("Ljava/util/concurrent/Semaphore") ||
            descriptor.starts_with("Ljava/util/concurrent/CyclicBarrier") ||
            descriptor.starts_with("Ljava/text/ChoiceFormat") || descriptor.starts_with("Ljava/text/MessageFormat") ||
            descriptor.starts_with("Ljavax/security/auth/x500/") || descriptor.starts_with("Lorg/apache/harmony/security/")) {
            const auto check = [&](VmMethodId method) {
                if (!(f.linker.Method(method).access_flags & kAccNative))
                    CHECK(f.linker.Method(method).kind != MethodKind::intrinsic);
            };
            for (const auto method : f.linker.Class(type).own_virtual_methods) check(method);
            for (const auto method : f.linker.Class(type).own_direct_methods) check(method);
        }
        if (!descriptor.starts_with("Ljava/util/") ||
            descriptor.starts_with("Ljava/util/concurrent/Executors")) continue;
        // All newly selected collection implementations are ordinary DEX.
        const auto collection = f.linker.ResolveDescriptor("Ljava/util/Collection;");
        const auto map = f.linker.ResolveDescriptor("Ljava/util/Map;");
        if (!f.linker.IsAssignable(collection, type) && !f.linker.IsAssignable(map, type)) continue;
        for (const auto method : f.linker.Class(type).own_virtual_methods)
            CHECK(f.linker.Method(method).kind != MethodKind::intrinsic);
        for (const auto method : f.linker.Class(type).own_direct_methods)
            CHECK(f.linker.Method(method).kind != MethodKind::intrinsic);
    }
    CHECK(count == 566);
}

TEST_CASE("DVM-103 bounded queues and Collections wrappers use API19 semantics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        const auto value = f.vm.NewStringUtf8("item");
        for (const auto* name : {"ArrayBlockingQueue", "LinkedBlockingQueue", "LinkedBlockingDeque"}) {
            CAPTURE(name);
            const auto descriptor = std::string("Ljava/util/concurrent/") + name + ";";
            const auto q = f.vm.NewIntrinsicInstance(descriptor);
            f.Construct(q, descriptor, "(I)V", {VmValue::Int(1)});
            const auto offer = [&] { return f.Virtual(q, "offer", "(Ljava/lang/Object;)Z", {VmValue::Ref(value)}); };
            auto result = offer(); f.RequireOk(result); CHECK(result.value.AsInt() == 1);
            result = offer(); f.RequireOk(result); CHECK(result.value.AsInt() == 0);
            result = f.Virtual(q, "poll", "()Ljava/lang/Object;"); f.RequireOk(result); CHECK(result.value.ref == value);
            result = f.Virtual(q, "poll", "()Ljava/lang/Object;"); f.RequireOk(result); CHECK_FALSE(result.value.ref.IsValid());
        }
        for (const auto* name : {"SynchronousQueue", "LinkedTransferQueue"}) {
            CAPTURE(name);
            const auto descriptor = std::string("Ljava/util/concurrent/") + name + ";";
            const auto q = f.vm.NewIntrinsicInstance(descriptor);
            f.Construct(q, descriptor, "()V");
            const auto result = f.Virtual(q, "poll", "()Ljava/lang/Object;");
            f.RequireOk(result); CHECK_FALSE(result.value.ref.IsValid());
        }
        const auto list = f.vm.NewIntrinsicInstance("Ljava/util/ArrayList;");
        f.Construct(list, "Ljava/util/ArrayList;", "()V");
        f.RequireOk(f.Virtual(list, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(value)}));
        const auto view = f.Static("Ljava/util/Collections;", "unmodifiableList",
            "(Ljava/util/List;)Ljava/util/List;", {VmValue::Ref(list)}); f.RequireOk(view);
        const auto rejected = f.Virtual(view.value.ref, "clear", "()V");
        REQUIRE(rejected.exception.IsValid());
        CHECK(f.linker.Class(rejected.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        f.RequireOk(f.Virtual(list, "clear", "()V"));
        const auto size = f.Virtual(view.value.ref, "size", "()I"); f.RequireOk(size); CHECK(size.value.AsInt() == 0);
    }
}

TEST_CASE("DVM-103 collection VM primitives use injected time and wrapping atomics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        auto time = f.Static("Ljava/lang/System;", "nanoTime", "()J");
        REQUIRE(time.exception.IsValid());
        CHECK(f.linker.Class(time.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        f.vm.Monitors().SetTimeSource([] { return std::int64_t{123}; });
        time = f.Static("Ljava/lang/System;", "nanoTime", "()J");
        f.RequireOk(time); CHECK(time.value.AsLong() == 123000000);
        const auto runtime = f.Static("Ljava/lang/Runtime;", "getRuntime", "()Ljava/lang/Runtime;");
        f.RequireOk(runtime);
        const auto processors = f.Virtual(runtime.value.ref, "availableProcessors", "()I");
        f.RequireOk(processors); CHECK(processors.value.AsInt() == 1);
        const auto atomic = f.vm.NewIntrinsicInstance("Ljava/util/concurrent/atomic/AtomicInteger;");
        f.Construct(atomic, "Ljava/util/concurrent/atomic/AtomicInteger;", "(I)V", {VmValue::Int(2147483647)});
        const auto old = f.Virtual(atomic, "getAndAdd", "(I)I", {VmValue::Int(1)});
        f.RequireOk(old); CHECK(old.value.AsInt() == 2147483647);
        const auto wrapped = f.Virtual(atomic, "get", "()I");
        f.RequireOk(wrapped); CHECK(wrapped.value.AsInt() == (-2147483647 - 1));
    }
}

TEST_CASE("DVM-103 deferred intrinsic interfaces preserve declaration order") {
    DexClassLinker linker;
    auto catalog = CoreIntrinsicCatalog();
    auto probe = IntrinsicClassBuilder::Class("Ltest/InterfaceOrder;", "Ljava/lang/Object;",
        {"Ljava/io/Serializable;", "Ljava/lang/Cloneable;"});
    catalog.push_back(std::move(probe).Build());
    linker.RegisterIntrinsics(catalog);
    linker.RegisterBootDex(Dvm102BootDex());
    linker.Link();
    const auto owner = linker.ResolveDescriptor("Ltest/InterfaceOrder;");
    const auto& interfaces = linker.Class(owner).direct_interfaces;
    REQUIRE(interfaces.size() == 2);
    CHECK(linker.Class(interfaces[0]).descriptor == "Ljava/io/Serializable;");
    CHECK(linker.Class(interfaces[1]).descriptor == "Ljava/lang/Cloneable;");
}

TEST_CASE("DVM-104 tools events atomics and X500 execute API19 bytecode") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        const auto text = f.vm.NewStringUtf8("one,two,,three");
        const auto tokenizer = f.vm.NewIntrinsicInstance("Ljava/util/StringTokenizer;");
        f.Construct(tokenizer, "Ljava/util/StringTokenizer;", "(Ljava/lang/String;Ljava/lang/String;)V",
            {VmValue::Ref(text), VmValue::Ref(f.vm.NewStringUtf8(","))});
        auto result = f.Virtual(tokenizer, "countTokens", "()I"); f.RequireOk(result); CHECK(result.value.AsInt() == 3);
        result = f.Virtual(tokenizer, "nextToken", "()Ljava/lang/String;"); f.RequireOk(result); CHECK(f.vm.StringUtf8(result.value.ref) == "one");
        const auto bitset = f.vm.NewIntrinsicInstance("Ljava/util/BitSet;"); f.Construct(bitset, "Ljava/util/BitSet;", "()V");
        f.RequireOk(f.Virtual(bitset, "set", "(I)V", {VmValue::Int(130)}));
        result = f.Virtual(bitset, "nextSetBit", "(I)I", {VmValue::Int(0)}); f.RequireOk(result); CHECK(result.value.AsInt() == 130);
        result = f.Static("Ljava/util/Objects;", "equals", "(Ljava/lang/Object;Ljava/lang/Object;)Z", {VmValue::Ref(text), VmValue::Ref(text)}); f.RequireOk(result); CHECK(result.value.AsInt() == 1);
        const auto math = f.vm.NewIntrinsicInstance("Ljava/math/MathContext;"); f.Construct(math, "Ljava/math/MathContext;", "(I)V", {VmValue::Int(7)});
        result = f.Virtual(math, "getPrecision", "()I"); f.RequireOk(result); CHECK(result.value.AsInt() == 7);
        const auto atomic = f.vm.NewIntrinsicInstance("Ljava/util/concurrent/atomic/AtomicLong;");
        f.Construct(atomic, "Ljava/util/concurrent/atomic/AtomicLong;", "(J)V", {VmValue::Long(INT64_MAX)});
        result = f.Virtual(atomic, "incrementAndGet", "()J"); f.RequireOk(result); CHECK(result.value.AsLong() == INT64_MIN);
        const auto principal = f.vm.NewIntrinsicInstance("Ljavax/security/auth/x500/X500Principal;");
        f.Construct(principal, "Ljavax/security/auth/x500/X500Principal;", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("CN=Alice,O=Example,C=US"))});
        result = f.Virtual(principal, "getName", "(Ljava/lang/String;)Ljava/lang/String;", {VmValue::Ref(f.vm.NewStringUtf8("CANONICAL"))});
        f.RequireOk(result); CHECK(f.vm.StringUtf8(result.value.ref) == "cn=alice,o=example,c=us");
        const auto encoded = f.Virtual(principal, "getEncoded", "()[B"); f.RequireOk(encoded);
        const auto decoded = f.vm.NewIntrinsicInstance("Ljavax/security/auth/x500/X500Principal;");
        f.Construct(decoded, "Ljavax/security/auth/x500/X500Principal;", "([B)V", {VmValue::Ref(encoded.value.ref)});
        result = f.Virtual(principal, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(decoded)}); f.RequireOk(result); CHECK(result.value.AsInt() == 1);
    }
}

TEST_CASE("DVM-104 memory wrappers preserve underlying guest stream identity") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        const auto bytes = f.vm.NewIntrinsicInstance("Ljava/io/ByteArrayOutputStream;");
        f.Construct(bytes, "Ljava/io/ByteArrayOutputStream;", "()V");
        const auto buffered = f.vm.NewIntrinsicInstance("Ljava/io/BufferedOutputStream;");
        f.Construct(buffered, "Ljava/io/BufferedOutputStream;", "(Ljava/io/OutputStream;I)V", {VmValue::Ref(bytes), VmValue::Int(2)});
        const auto data = f.vm.NewIntrinsicInstance("Ljava/io/DataOutputStream;");
        f.Construct(data, "Ljava/io/DataOutputStream;", "(Ljava/io/OutputStream;)V", {VmValue::Ref(buffered)});
        const auto text = f.vm.NewStringUtf8("Hello 世界");
        f.RequireOk(f.Virtual(data, "writeUTF", "(Ljava/lang/String;)V", {VmValue::Ref(text)}));
        f.RequireOk(f.Virtual(data, "flush", "()V"));
        const auto encoded = f.Virtual(bytes, "toByteArray", "()[B"); f.RequireOk(encoded);
        CHECK(f.model.ArrayLength(encoded.value.ref) == 14);
        const auto source = f.vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
        f.Construct(source, "Ljava/io/ByteArrayInputStream;", "([B)V", {VmValue::Ref(encoded.value.ref)});
        const auto input = f.vm.NewIntrinsicInstance("Ljava/io/DataInputStream;");
        f.Construct(input, "Ljava/io/DataInputStream;", "(Ljava/io/InputStream;)V", {VmValue::Ref(source)});
        const auto decoded = f.Virtual(input, "readUTF", "()Ljava/lang/String;"); f.RequireOk(decoded);
        CHECK(f.vm.StringUtf8(decoded.value.ref) == "Hello 世界");
        const auto remaining = f.Virtual(source, "available", "()I"); f.RequireOk(remaining); CHECK(remaining.value.AsInt() == 0);
        const auto sr = f.vm.NewIntrinsicInstance("Ljava/io/StringReader;");
        f.Construct(sr, "Ljava/io/StringReader;", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("first\r\nsecond"))});
        const auto br = f.vm.NewIntrinsicInstance("Ljava/io/BufferedReader;");
        f.Construct(br, "Ljava/io/BufferedReader;", "(Ljava/io/Reader;I)V", {VmValue::Ref(sr), VmValue::Int(2)});
        auto line=f.Virtual(br,"readLine","()Ljava/lang/String;");f.RequireOk(line);CHECK(f.vm.StringUtf8(line.value.ref)=="first");
        line=f.Virtual(br,"readLine","()Ljava/lang/String;");f.RequireOk(line);CHECK(f.vm.StringUtf8(line.value.ref)=="second");
        f.RequireOk(f.Virtual(br,"close","()V"));
        const auto closed=f.Virtual(sr,"read","()I");REQUIRE(closed.exception.IsValid());CHECK(f.linker.Class(closed.exception_class).descriptor=="Ljava/io/IOException;");
    }
}

TEST_CASE("DVM-104 standard charset names encode decode and incremental reader agree") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        for (const auto* name : {"UTF-8", "UTF-16", "UTF-16BE", "UTF-16LE", "ISO-8859-1", "US-ASCII"}) {
            CAPTURE(name);
            const auto charset=f.Static("Ljava/nio/charset/Charset;","forName","(Ljava/lang/String;)Ljava/nio/charset/Charset;",{VmValue::Ref(f.vm.NewStringUtf8(name))});f.RequireOk(charset);
            const auto text=f.model.NewString(std::string(name).starts_with("UTF")?u"A世界😀":u"Abc");
            const auto bytes=f.Virtual(text,"getBytes","(Ljava/nio/charset/Charset;)[B",{VmValue::Ref(charset.value.ref)});f.RequireOk(bytes);
            const auto decoded=f.vm.NewIntrinsicInstance("Ljava/lang/String;");
            f.Construct(decoded,"Ljava/lang/String;","([BLjava/nio/charset/Charset;)V",{VmValue::Ref(bytes.value.ref),VmValue::Ref(charset.value.ref)});
            CHECK(f.vm.StringUtf8(decoded)==f.vm.StringUtf8(text));
            const auto source=f.vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");f.Construct(source,"Ljava/io/ByteArrayInputStream;","([B)V",{VmValue::Ref(bytes.value.ref)});
            const auto reader=f.vm.NewIntrinsicInstance("Ljava/io/InputStreamReader;");f.Construct(reader,"Ljava/io/InputStreamReader;","(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V",{VmValue::Ref(source),VmValue::Ref(charset.value.ref)});
            std::u16string result;
            for (int i=0;i<32;++i) {const auto unit=f.Virtual(reader,"read","()I");f.RequireOk(unit);if(unit.value.AsInt()<0)break;result.push_back(static_cast<char16_t>(unit.value.AsInt()));}
            CHECK(result==f.model.StringValue(text));
            const auto encoding=f.Virtual(reader,"getEncoding","()Ljava/lang/String;"); f.RequireOk(encoding); CHECK(f.vm.StringUtf8(encoding.value.ref)==name);
            const auto named=f.vm.NewIntrinsicInstance("Ljava/lang/String;"); f.Construct(named,"Ljava/lang/String;","([BLjava/lang/String;)V",{VmValue::Ref(bytes.value.ref),VmValue::Ref(f.vm.NewStringUtf8(name))}); CHECK(f.model.StringValue(named)==f.model.StringValue(text));
            const auto ordering=f.Virtual(charset.value.ref,"compareTo","(Ljava/nio/charset/Charset;)I",{VmValue::Ref(charset.value.ref)}); f.RequireOk(ordering); CHECK(ordering.value.AsInt()==0);

            f.RequireOk(f.Virtual(reader,"close","()V"));
        }
        const auto malformed=f.model.NewString(std::u16string(1,static_cast<char16_t>(0xd800)));
        for (const auto* name : {"UTF-8", "UTF-16BE", "UTF-16LE", "US-ASCII"}) {
            const auto bytes=f.Virtual(malformed,"getBytes","(Ljava/lang/String;)[B",{VmValue::Ref(f.vm.NewStringUtf8(name))}); f.RequireOk(bytes);
            const auto decoded=f.vm.NewIntrinsicInstance("Ljava/lang/String;");
            f.Construct(decoded,"Ljava/lang/String;","([BLjava/lang/String;)V",{VmValue::Ref(bytes.value.ref),VmValue::Ref(f.vm.NewStringUtf8(name))});
            CHECK(f.model.StringValue(decoded)==(std::string_view(name).starts_with("UTF-16")?u"\ufffd":u"?"));
        }
        const auto invalid=f.Static("Ljava/nio/charset/Charset;","forName","(Ljava/lang/String;)Ljava/nio/charset/Charset;",{VmValue::Ref(f.vm.NewStringUtf8("not a charset"))});
        REQUIRE(invalid.exception.IsValid());CHECK(f.linker.Class(invalid.exception_class).descriptor=="Ljava/nio/charset/IllegalCharsetNameException;");
        const auto unsupported=f.Static("Ljava/nio/charset/Charset;","forName","(Ljava/lang/String;)Ljava/nio/charset/Charset;",{VmValue::Ref(f.vm.NewStringUtf8("made-up"))});
        REQUIRE(unsupported.exception.IsValid());CHECK(f.linker.Class(unsupported.exception_class).descriptor=="Ljava/nio/charset/UnsupportedCharsetException;");
    }
}

TEST_CASE("DVM-104 formatters events tokenizer and key parameters use guest state") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto calls=std::make_shared<int>();
        auto listener=IntrinsicClassBuilder::Class("Ltest/PropertyListener;","Ljava/lang/Object;",{"Ljava/beans/PropertyChangeListener;"});
        listener.VirtualMethod("propertyChange","(Ljava/beans/PropertyChangeEvent;)V",[calls](IntrinsicContext&){++*calls;return VmValue::Void();});
        const std::vector<IntrinsicClassDecl> extras{std::move(listener).Build()};
        Dvm87Vm f(backend,"en","eng","USA","GMT",extras);
        const auto events=f.vm.NewIntrinsicInstance("Ljava/beans/PropertyChangeSupport;");
        const auto source=f.vm.NewIntrinsicInstance("Ljava/lang/Object;");
        f.Construct(events,"Ljava/beans/PropertyChangeSupport;","(Ljava/lang/Object;)V",{VmValue::Ref(source)});
        const auto observer=f.vm.NewIntrinsicInstance("Ltest/PropertyListener;");
        f.RequireOk(f.Virtual(events,"addPropertyChangeListener","(Ljava/beans/PropertyChangeListener;)V",{VmValue::Ref(observer)}));
        const auto fire=[&](int old,int next){return f.Virtual(events,"firePropertyChange","(Ljava/lang/String;II)V",{VmValue::Ref(f.vm.NewStringUtf8("value")),VmValue::Int(old),VmValue::Int(next)});};
        f.RequireOk(fire(1,1));CHECK(*calls==0);f.RequireOk(fire(1,2));CHECK(*calls==1);
        f.RequireOk(f.Virtual(events,"removePropertyChangeListener","(Ljava/beans/PropertyChangeListener;)V",{VmValue::Ref(observer)}));f.RequireOk(fire(2,3));CHECK(*calls==1);
        const auto choice=f.vm.NewIntrinsicInstance("Ljava/text/ChoiceFormat;");
        f.Construct(choice,"Ljava/text/ChoiceFormat;","(Ljava/lang/String;)V",{VmValue::Ref(f.vm.NewStringUtf8("0#none|1#one|1<many"))});
        const auto rendered=f.Virtual(choice,"format","(J)Ljava/lang/String;",{VmValue::Long(2)});f.RequireOk(rendered);CHECK(f.vm.StringUtf8(rendered.value.ref)=="many");
        const auto format=f.vm.NewIntrinsicInstance("Ljava/text/MessageFormat;");
        f.Construct(format,"Ljava/text/MessageFormat;","(Ljava/lang/String;)V",{VmValue::Ref(f.vm.NewStringUtf8("Hello {0}"))});
        const auto args=f.model.NewObjectArray(f.linker.ResolveDescriptor("[Ljava/lang/Object;"),f.linker.ResolveDescriptor("Ljava/lang/Object;"),1);
        f.model.SetObjectElement(args,0,f.vm.NewStringUtf8("world"));
        const auto message=f.Virtual(format,"format","(Ljava/lang/Object;)Ljava/lang/String;",{VmValue::Ref(args)});f.RequireOk(message);CHECK(f.vm.StringUtf8(message.value.ref)=="Hello world");
        const auto reader=f.vm.NewIntrinsicInstance("Ljava/io/StringReader;");f.Construct(reader,"Ljava/io/StringReader;","(Ljava/lang/String;)V",{VmValue::Ref(f.vm.NewStringUtf8("word 42"))});
        const auto tokenizer=f.vm.NewIntrinsicInstance("Ljava/io/StreamTokenizer;");f.Construct(tokenizer,"Ljava/io/StreamTokenizer;","(Ljava/io/Reader;)V",{VmValue::Ref(reader)});
        const auto token=f.Virtual(tokenizer,"nextToken","()I");f.RequireOk(token);CHECK(token.value.AsInt()==-3);
        const auto keybytes=f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"),JniPrimitiveKind::byte,3);
        f.model.WriteByteRegion(keybytes,0,std::array{std::byte{1},std::byte{2},std::byte{3}});
        for(const auto* owner:{"Ljavax/crypto/spec/IvParameterSpec;","Ljavax/crypto/spec/SecretKeySpec;","Ljava/security/spec/PKCS8EncodedKeySpec;","Ljava/security/spec/X509EncodedKeySpec;"}) {
            CAPTURE(owner);const auto key=f.vm.NewIntrinsicInstance(owner);
            const bool secret=std::string_view(owner).find("SecretKey")!=std::string_view::npos;
            if(secret)f.Construct(key,owner,"([BLjava/lang/String;)V",{VmValue::Ref(keybytes),VmValue::Ref(f.vm.NewStringUtf8("AES"))});
            else f.Construct(key,owner,"([B)V",{VmValue::Ref(keybytes)});
            const auto copied=f.Virtual(key,std::string_view(owner).find("IvParameter")!=std::string_view::npos?"getIV":"getEncoded","()[B");f.RequireOk(copied);CHECK(copied.value.ref!=keybytes);CHECK(f.model.ReadByteRegion(copied.value.ref,0,3)==f.model.ReadByteRegion(keybytes,0,3));
        }
    }
}

TEST_CASE("DVM-104 synchronizers park real guest threads and handle interruption") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const auto* descriptor : {"Ljava/util/concurrent/CountDownLatch;", "Ljava/util/concurrent/Semaphore;", "Ljava/util/concurrent/CyclicBarrier;"}) {
            CAPTURE(std::string(descriptor));
            for (const bool interrupt : {false, true}) {
                CAPTURE(interrupt);
                auto sync=std::make_shared<VmObjectRef>(); auto finished=std::make_shared<std::atomic<bool>>(false);auto interrupted=std::make_shared<std::atomic<bool>>(false);
                auto runner=IntrinsicClassBuilder::Class("Ltest/SyncRunner;","Ljava/lang/Object;",{"Ljava/lang/Runnable;"});
                const bool barrier=std::string_view(descriptor).find("CyclicBarrier")!=std::string_view::npos;
                const bool semaphore=std::string_view(descriptor).find("Semaphore")!=std::string_view::npos;
                runner.VirtualMethod("run","()V",[sync,finished,interrupted,barrier,semaphore](IntrinsicContext& c) {
                    const auto cls=c.vm.Model().ObjectClass(*sync);
                    const auto slot=c.vm.Linker().FindVtableIndex(cls,semaphore?"acquire":"await",barrier?"()I":"()V");
                    const std::array args{VmValue::Ref(*sync)};const auto result=c.vm.Call(c.vm.Linker().Class(cls).vtable[*slot],args);
                    if(result.exception.IsValid()) {
                        if(c.vm.Linker().Class(result.exception_class).descriptor=="Ljava/lang/InterruptedException;") *interrupted=true;
                        else c.vm.SetPendingException(result.exception);
                    }
                    *finished=true;return VmValue::Void();
                });
                const std::vector<IntrinsicClassDecl> extras{std::move(runner).Build()};Dvm87Vm f(backend,"en","eng","USA","GMT",extras);
                f.vm.Monitors().SetTimeSource([] {return std::int64_t{0};});
                *sync=f.vm.NewIntrinsicInstance(descriptor);f.Construct(*sync,descriptor,"(I)V",{VmValue::Int(barrier?2:semaphore?0:1)});
                const std::array roots{*sync};const auto protect=f.vm.ProtectReferences(roots);
                const auto target=f.vm.NewIntrinsicInstance("Ltest/SyncRunner;");
                const auto thread=f.vm.NewIntrinsicInstance("Ljava/lang/Thread;");f.Construct(thread,"Ljava/lang/Thread;","(Ljava/lang/Runnable;)V",{VmValue::Ref(target)});
                f.RequireOk(f.Virtual(thread,"start","()V"));
                bool parked=false;
                for(int i=0;i<3000;++i) {
                    for(const auto& t:f.threads.Snapshot()) if(t.object==thread.Value() && t.wait_state!=VmThreadWaitState::none)parked=true;
                    if(parked || finished->load())break;std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (!parked) { f.threads.Interrupt(thread); f.threads.Join(thread); }
                REQUIRE(parked);CHECK_FALSE(finished->load());
                if(interrupt)f.threads.Interrupt(thread);
                else if(barrier)f.RequireOk(f.Virtual(*sync,"await","()I"));
                else f.RequireOk(f.Virtual(*sync,semaphore?"release":"countDown","()V"));
                bool done=false;for(int i=0;i<3000;++i){if(finished->load()){done=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
                if(!done)f.threads.Interrupt(thread);
                f.threads.Join(thread);CHECK(done);CHECK(interrupted->load()==interrupt);const auto failure=f.threads.TakeFailure(); CHECK_MESSAGE(!failure, failure.value_or(""));
                if(barrier) {const auto broken=f.Virtual(*sync,"isBroken","()Z");f.RequireOk(broken);CHECK(broken.value.AsInt()==interrupt);f.RequireOk(f.Virtual(*sync,"reset","()V"));}
            }
        }
    }
}

TEST_CASE("DVM-104 NativeBN tokens are isolated checked and swept with BigInt owners") {
    Dvm87Vm f;
    const auto baseline=f.vm.BigInts().Size();
    const auto owner=f.vm.NewIntrinsicInstance("Ljava/math/BigInt;");f.Construct(owner,"Ljava/math/BigInt;","()V");
    auto call=[&](const char* name,const char* sig,std::vector<VmValue> args){return f.Virtual(owner,name,sig,args);};
    f.RequireOk(call("putLongInt","(J)V",{VmValue::Long(INT64_MIN)}));
    const auto field=f.linker.FindFieldRecursive(f.model.ObjectClass(owner),"bignum","J");REQUIRE(field);
    const auto slot=f.linker.Field(*field).slot;const auto slots=f.model.InstanceSlots(owner);
    const auto token=static_cast<std::uint64_t>(slots[slot].bits)|(static_cast<std::uint64_t>(slots[slot+1].bits)<<32U);
    CHECK(f.vm.BigInts().Require(token).magnitude==(UINT64_C(1)<<63U));
    CHECK(f.vm.BigInts().Size()==baseline+1);
    const auto unsupported=f.Static("Ljava/math/NativeBN;", "BN_add", "(JJJ)V", {VmValue::Long(static_cast<std::int64_t>(token)),VmValue::Long(static_cast<std::int64_t>(token)),VmValue::Long(static_cast<std::int64_t>(token))}); REQUIRE(unsupported.exception.IsValid());

    Dvm87Vm other;CHECK_THROWS(other.vm.BigInts().Require(token));
    static_cast<void>(f.vm.CollectGarbage());CHECK(f.vm.BigInts().Size()==baseline);CHECK_THROWS(f.vm.BigInts().Require(token));
}

TEST_CASE("DVM-104 atomic arrays references and immediate timeouts follow API19") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        f.vm.Monitors().SetTimeSource([] { return std::int64_t{123}; });
        for (const bool wide : {false, true}) {
            const auto type = std::string("Ljava/util/concurrent/atomic/Atomic") + (wide ? "LongArray;" : "IntegerArray;");
            const auto array = f.vm.NewIntrinsicInstance(type);
            f.Construct(array, type, "(I)V", {VmValue::Int(2)});
            auto result = f.Virtual(array, "compareAndSet", wide ? "(IJJ)Z" : "(III)Z",
                {VmValue::Int(1), wide ? VmValue::Long(0) : VmValue::Int(0), wide ? VmValue::Long(INT64_MAX) : VmValue::Int(INT32_MAX)});
            f.RequireOk(result); CHECK(result.value.AsInt() == 1);
            result = f.Virtual(array, "incrementAndGet", wide ? "(I)J" : "(I)I", {VmValue::Int(1)});
            f.RequireOk(result); CHECK((wide ? result.value.AsLong() : result.value.AsInt()) == (wide ? INT64_MIN : INT32_MIN));
            result = f.Virtual(array, "get", wide ? "(I)J" : "(I)I", {VmValue::Int(2)});
            REQUIRE(result.exception.IsValid()); CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/IndexOutOfBoundsException;");
        }
        const auto original = f.vm.NewStringUtf8("old"), next = f.vm.NewStringUtf8("new");
        for (const bool marked : {false, true}) {
            const auto type = std::string("Ljava/util/concurrent/atomic/Atomic") + (marked ? "MarkableReference;" : "StampedReference;");
            const auto reference = f.vm.NewIntrinsicInstance(type);
            f.Construct(reference, type, marked ? "(Ljava/lang/Object;Z)V" : "(Ljava/lang/Object;I)V", {VmValue::Ref(original), VmValue::Int(0)});
            const auto cas = f.Virtual(reference, "compareAndSet", marked ? "(Ljava/lang/Object;Ljava/lang/Object;ZZ)Z" : "(Ljava/lang/Object;Ljava/lang/Object;II)Z",
                {VmValue::Ref(original), VmValue::Ref(next), VmValue::Int(0), VmValue::Int(1)});
            f.RequireOk(cas); CHECK(cas.value.AsInt() == 1);
            const auto value = f.Virtual(reference, "getReference", "()Ljava/lang/Object;"); f.RequireOk(value); CHECK(value.value.ref == next);
        }
        const auto references = f.vm.NewIntrinsicInstance("Ljava/util/concurrent/atomic/AtomicReferenceArray;");
        f.Construct(references, "Ljava/util/concurrent/atomic/AtomicReferenceArray;", "(I)V", {VmValue::Int(1)});
        f.RequireOk(f.Virtual(references, "lazySet", "(ILjava/lang/Object;)V", {VmValue::Int(0), VmValue::Ref(next)}));
        const std::array roots{references}; const auto protect = f.vm.ProtectReferences(roots);
        static_cast<void>(f.vm.CollectGarbage());
        const auto kept = f.Virtual(references, "get", "(I)Ljava/lang/Object;", {VmValue::Int(0)}); f.RequireOk(kept); CHECK(f.vm.StringUtf8(kept.value.ref) == "new");
        const auto unit_type = f.linker.ResolveDescriptor("Ljava/util/concurrent/TimeUnit;");
        f.RequireOk(f.vm.EnsureClassInitialized(unit_type));
        const auto unit_field = f.linker.FindFieldRecursive(unit_type, "MILLISECONDS", "Ljava/util/concurrent/TimeUnit;"); REQUIRE(unit_field);
        const auto unit = VmObjectRef(f.linker.Class(unit_type).static_storage[f.linker.Field(*unit_field).slot]);
        for (const auto* type : {"Ljava/util/concurrent/CountDownLatch;", "Ljava/util/concurrent/Semaphore;", "Ljava/util/concurrent/CyclicBarrier;"}) {
            const bool barrier = std::string_view(type).find("CyclicBarrier") != std::string_view::npos;
            const bool semaphore = std::string_view(type).find("Semaphore") != std::string_view::npos;
            const auto sync = f.vm.NewIntrinsicInstance(type); f.Construct(sync, type, "(I)V", {VmValue::Int(barrier ? 2 : semaphore ? 0 : 1)});
            const auto result = f.Virtual(sync, semaphore ? "tryAcquire" : "await", barrier ? "(JLjava/util/concurrent/TimeUnit;)I" : "(JLjava/util/concurrent/TimeUnit;)Z", {VmValue::Long(0), VmValue::Ref(unit)});
            if (barrier) {
                REQUIRE(result.exception.IsValid()); CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/util/concurrent/TimeoutException;");
                const auto broken = f.Virtual(sync, "isBroken", "()Z"); f.RequireOk(broken); CHECK(broken.value.AsInt() == 1);
            } else { f.RequireOk(result); CHECK(result.value.AsInt() == 0); }
        }
    }
}

TEST_CASE("DVM-104 guest stream callbacks retain monitor exception identity and nonblocking available") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto failure = std::make_shared<VmObjectRef>();
        auto reads = std::make_shared<int>();
        auto locked = std::make_shared<bool>(false);
        auto input = IntrinsicClassBuilder::Class("Ltest/CallbackInput;", "Ljava/io/InputStream;");
        input.OverrideMethod("read", "()I", [failure, reads, locked](IntrinsicContext& c) {
            *locked = c.vm.Monitors().IsOwner(c.receiver, c.vm.CurrentContextToken());
            ++*reads;
            static_cast<void>(c.vm.CollectGarbage("stream-callback"));
            c.vm.SetPendingException(*failure); return VmValue::Int(-1);
        });
        const std::vector<IntrinsicClassDecl> extras{std::move(input).Build()};
        Dvm87Vm f(backend,"en","eng","USA","GMT",extras);
        *failure=f.vm.NewIntrinsicInstance("Ljava/io/IOException;");
        const std::array roots{*failure}; const auto protect=f.vm.ProtectReferences(roots);
        const auto source=f.vm.NewIntrinsicInstance("Ltest/CallbackInput;");
        const auto reader=f.vm.NewIntrinsicInstance("Ljava/io/InputStreamReader;");
        f.Construct(reader,"Ljava/io/InputStreamReader;","(Ljava/io/InputStream;)V",{VmValue::Ref(source)});
        const auto result=f.Virtual(reader,"read","()I"); CHECK(result.exception==*failure); CHECK(*locked); CHECK(*reads==1);
        CHECK_FALSE(f.vm.Monitors().IsOwner(source,f.vm.CurrentContextToken()));
        // A stream whose header is available but whose next byte must not be read by available().
        auto& state=f.vm.IO();
        const auto object=f.vm.NewIntrinsicInstance("Ljava/io/ObjectInputStream;");
        IoRuntime::InputState protocol; protocol.source=source;
        state.SetInput(object,std::move(protocol)); state.BeginObjectInput(object);
        const auto available=f.Virtual(object,"available","()I"); f.RequireOk(available); CHECK(available.value.AsInt()==0); CHECK(*reads==1);
    }
}
