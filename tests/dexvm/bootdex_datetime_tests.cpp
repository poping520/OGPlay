#include "boot_dex.h"
#include <doctest/doctest.h>

#include <cstdint>
#include <algorithm>
#include <memory>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include "ogplay/runtime/integration/dexvm_android.h"
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
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "tls_fixtures.h"

#include "bootdex_library_fixture.h"

using namespace ogplay::test::bootdex_library;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

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

TEST_CASE("DVM-113 Locale ISO catalogs use pinned ICU and independent Java clones" *
          doctest::skip(true)) {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        for (const auto* language : {"zh", "en"}) {
            Dvm87Vm f(backend, language);
            struct Catalog {
                const char* method;
                const char* cache;
                JniSize count;
                std::uint64_t hash;
                const char* present;
                const char* absent;
            };
            // Golden counts and FNV-1a of null-delimited codes, in source order,
            // from pinned ICU 51 common/uloc.cpp up to each list's first NULL.
            // The language list contains three-letter codes, but excludes iw/in/ji.
            for (const auto& catalog : {
                     Catalog{"getISOLanguages", "isoLanguages", 559,
                             0xd8abccb8fdfff723ULL, "fil", "iw"},
                     Catalog{"getISOCountries", "isoCountries", 249,
                             0x0589c3fab47c6b84ULL, "CN", "AN"}}) {
                CAPTURE(catalog.method);
                CAPTURE(language);
                CAPTURE(backend);
                const auto invoke = [&](const char* owner) {
                    const auto result = f.Static(owner, catalog.method,
                                                 "()[Ljava/lang/String;");
                    Dvm87Vm::RequireOk(result);
                    return result.value.ref;
                };
                const auto first = invoke("Ljava/util/Locale;");
                const auto first_root = f.vm.ProtectReferences(std::array{first});
                CHECK(f.model.ObjectClass(first) ==
                      f.linker.ResolveDescriptor("[Ljava/lang/String;"));
                REQUIRE(f.model.ArrayLength(first) == catalog.count);
                std::uint64_t hash = 14695981039346656037ULL;
                for (JniSize index = 0; index < catalog.count; ++index) {
                    const auto code = f.vm.StringUtf8(f.model.GetObjectElement(first, index));
                    for (const char byte : code)
                        hash = (hash ^ static_cast<unsigned char>(byte)) * 1099511628211ULL;
                    hash *= 1099511628211ULL; // Null separator.
                }
                CHECK(hash == catalog.hash);
                // Exercise the caller's Arrays.asList(...).contains(...) path.
                const auto list = f.Static("Ljava/util/Arrays;", "asList",
                    "([Ljava/lang/Object;)Ljava/util/List;", {VmValue::Ref(first)});
                Dvm87Vm::RequireOk(list);
                const auto list_root = f.vm.ProtectReferences(std::array{list.value.ref});
                for (const auto& [text, expected] : {
                         std::pair{catalog.present, 1}, std::pair{catalog.absent, 0},
                         std::pair{"not-an-ISO-code", 0}}) {
                    const auto contains = f.Virtual(list.value.ref, "contains", "(Ljava/lang/Object;)Z",
                        {VmValue::Ref(f.vm.NewStringUtf8(text))});
                    Dvm87Vm::RequireOk(contains);
                    CHECK(contains.value.AsInt() == expected);
                }
                const auto original = f.model.GetObjectElement(first, 0);
                const auto direct = invoke("Llibcore/icu/ICU;");
                const auto direct_root = f.vm.ProtectReferences(std::array{direct});
                CHECK(direct != first);
                CHECK(f.model.GetObjectElement(direct, 0) == original);
                const auto cache = f.linker.FindFieldRecursive(
                    f.linker.ResolveDescriptor("Llibcore/icu/ICU;"),
                    catalog.cache, "[Ljava/lang/String;");
                REQUIRE(cache.has_value());
                const auto& field = f.linker.Field(*cache);
                const auto cached = VmObjectRef(f.linker.Class(field.owner).static_storage[field.slot]);
                CHECK(cached != first);
                CHECK(cached != direct);
                f.model.SetObjectElement(first, 0, f.vm.NewStringUtf8("changed"));
                f.model.SetObjectElement(direct, 0, VmObjectRef{});
                static_cast<void>(f.vm.CollectGarbage("iso-catalog-cache"));
                const auto next = invoke("Ljava/util/Locale;");
                CHECK(next != first);
                CHECK(next != direct);
                CHECK(f.model.GetObjectElement(next, 0) == original);
                CHECK(f.model.ArrayLength(next) == catalog.count);
                CHECK(f.model.GetObjectElement(cached, 0) == original);
            }
        }
    }
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

TEST_CASE("DVM-102 SimpleDateFormat initializes the API 19 object graph" *
          doctest::skip(true)) {
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

    const auto formatter_count = fixture.vm.GuestNativeResourceCount();
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
    CHECK(fixture.vm.GuestNativeResourceCount() == formatter_count + 1);
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
    CHECK(transient_token != 0U);
    const auto reachable = fixture.vm.MarkReachable();
    CHECK_FALSE(reachable.IsMarked(transient_formatter));
    static_cast<void>(fixture.vm.SweepGarbage(reachable));
    CHECK(fixture.vm.GuestNativeResourceCount() <= formatter_count);
  }
}

TEST_CASE("DVM-102 date formatting covers constructors factories and zones" *
          doctest::skip(true)) {
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

TEST_CASE("DVM-102 regression comma literal survives disabled grouping in date parser" *
          doctest::skip(true)) {
 for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
  Dvm87Vm f(backend);
  auto fmt=f.vm.NewIntrinsicInstance("Ljava/text/SimpleDateFormat;");
  f.Construct(fmt, "Ljava/text/SimpleDateFormat;", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("yyyy,MM,dd"))});
  auto parsed=f.Virtual(fmt,"parse","(Ljava/lang/String;)Ljava/util/Date;",{VmValue::Ref(f.vm.NewStringUtf8("2024,01,02"))});
  CHECK_MESSAGE(!parsed.exception.IsValid(), parsed.exception_message);
  if (!parsed.exception.IsValid()) CHECK(f.Virtual(parsed.value.ref,"getTime","()J").value.AsLong()==1704153600000LL);
 }
}

TEST_CASE("DVM-102 regression Locale constructor normalizes supported en_US" *
          doctest::skip(true)) {
 Dvm87Vm f;
 auto locale=f.vm.NewIntrinsicInstance("Ljava/util/Locale;");
 f.Construct(locale,"Ljava/util/Locale;","(Ljava/lang/String;Ljava/lang/String;)V",{VmValue::Ref(f.vm.NewStringUtf8("EN")),VmValue::Ref(f.vm.NewStringUtf8("us"))});
 CHECK(f.vm.StringUtf8(f.Virtual(locale,"toString","()Ljava/lang/String;").value.ref)=="en_US");
 auto fmt=f.vm.NewIntrinsicInstance("Ljava/text/SimpleDateFormat;");
 auto result=f.Static("Ljava/text/SimpleDateFormat;","<init>","(Ljava/lang/String;Ljava/util/Locale;)V",{VmValue::Ref(fmt),VmValue::Ref(f.vm.NewStringUtf8("yyyy")),VmValue::Ref(locale)});
 CHECK_MESSAGE(!result.exception.IsValid(),result.exception_message);
}

TEST_CASE("DVM-102 regression narrow English date symbols are narrow" *
          doctest::skip(true)) {
 Dvm87Vm f(InterpreterBackend::switch_dispatch,"en","eng","USA");
 auto fmt=f.vm.NewIntrinsicInstance("Ljava/text/SimpleDateFormat;");
 f.Construct(fmt,"Ljava/text/SimpleDateFormat;","(Ljava/lang/String;)V",{VmValue::Ref(f.vm.NewStringUtf8("MMMMM EEEEE"))});
 auto date=f.vm.NewIntrinsicInstance("Ljava/util/Date;");
 f.Construct(date,"Ljava/util/Date;","(J)V",{VmValue::Long(0)});
 auto value=f.Virtual(fmt,"format","(Ljava/util/Date;)Ljava/lang/String;",{VmValue::Ref(date)});
 Dvm87Vm::RequireOk(value);
 CHECK(f.vm.StringUtf8(value.value.ref)=="J T");
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

TEST_CASE("DVM-102 regression native parse preserves API19 position semantics" *
          doctest::skip(true)) {
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

TEST_CASE("DVM-183 SQL date value types use API 19 BootDex") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        Dvm87Vm fixture(backend);
        const auto util_date =
            fixture.linker.ResolveDescriptor("Ljava/util/Date;");
        const auto sql_date =
            fixture.linker.ResolveDescriptor("Ljava/sql/Date;");
        const auto sql_time =
            fixture.linker.ResolveDescriptor("Ljava/sql/Time;");
        const auto sql_timestamp =
            fixture.linker.ResolveDescriptor("Ljava/sql/Timestamp;");
        for (const auto type : {sql_date, sql_time, sql_timestamp}) {
            fixture.linker.EnsureClassLinked(type);
            const auto& linked = fixture.linker.Class(type);
            CHECK(linked.is_boot_dex);
            CHECK_FALSE(linked.is_intrinsic);
            CHECK(linked.super.has_value());
            CHECK(*linked.super == util_date);
            CHECK(fixture.linker.IsAssignable(util_date, type));
            for (const auto method : linked.own_virtual_methods) {
                CHECK(fixture.linker.Method(method).kind !=
                      MethodKind::intrinsic);
            }
            for (const auto method : linked.own_direct_methods) {
                CHECK(fixture.linker.Method(method).kind !=
                      MethodKind::intrinsic);
            }
        }

        const auto date = fixture.vm.NewIntrinsicInstance("Ljava/sql/Date;");
        fixture.Construct(date, "Ljava/sql/Date;", "(J)V", {VmValue::Long(0)});
        auto outcome = fixture.Virtual(date, "getTime", "()J");
        fixture.RequireOk(outcome);
        CHECK(outcome.value.AsLong() == 0);
        outcome = fixture.Virtual(date, "toString", "()Ljava/lang/String;");
        fixture.RequireOk(outcome);
        CHECK(fixture.vm.StringUtf8(outcome.value.ref) == "1970-01-01");
        outcome = fixture.Virtual(date, "getHours", "()I");
        REQUIRE(outcome.exception.IsValid());
        CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
              "Ljava/lang/IllegalArgumentException;");

        const auto kept = fixture.vm.NewIntrinsicInstance("Ljava/sql/Date;");
        fixture.Construct(kept, "Ljava/sql/Date;", "(J)V",
                          {VmValue::Long(12345)});
        outcome = fixture.Virtual(kept, "getTime", "()J");
        fixture.RequireOk(outcome);
        CHECK(outcome.value.AsLong() == 12345);

        const auto time = fixture.vm.NewIntrinsicInstance("Ljava/sql/Time;");
        fixture.Construct(time, "Ljava/sql/Time;", "(J)V", {VmValue::Long(0)});
        outcome = fixture.Virtual(time, "getTime", "()J");
        fixture.RequireOk(outcome);
        CHECK(outcome.value.AsLong() == 0);
        outcome = fixture.Virtual(time, "toString", "()Ljava/lang/String;");
        fixture.RequireOk(outcome);
        CHECK(fixture.vm.StringUtf8(outcome.value.ref) == "00:00:00");

        constexpr auto millis = 1'704'067'200'123LL;
        const auto timestamp =
            fixture.vm.NewIntrinsicInstance("Ljava/sql/Timestamp;");
        fixture.Construct(timestamp, "Ljava/sql/Timestamp;", "(J)V",
                          {VmValue::Long(millis)});
        outcome = fixture.Virtual(timestamp, "getTime", "()J");
        fixture.RequireOk(outcome);
        CHECK(outcome.value.AsLong() == millis);
        outcome = fixture.Virtual(timestamp, "getNanos", "()I");
        fixture.RequireOk(outcome);
        CHECK(outcome.value.AsInt() == 123000000);
        fixture.RequireOk(fixture.Virtual(timestamp, "setNanos", "(I)V",
                                          {VmValue::Int(7)}));
        outcome = fixture.Virtual(timestamp, "getNanos", "()I");
        fixture.RequireOk(outcome);
        CHECK(outcome.value.AsInt() == 7);
        outcome = fixture.Virtual(timestamp, "getTime", "()J");
        fixture.RequireOk(outcome);
        CHECK(outcome.value.AsLong() == 1'704'067'200'000LL);
    }
}
