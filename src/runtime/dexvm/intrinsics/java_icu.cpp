#include "catalog.h"
#include "shared.h"

#include <array>
#include <memory>
#include "../icu_support.h"
#include <unicode/calendar.h>
#include <unicode/dtfmtsym.h>
#include <unicode/dcfmtsym.h>
#include <unicode/decimfmt.h>
#include <unicode/dtptngen.h>
#include <unicode/ucurr.h>
#include <unicode/ures.h>
#include <unicode/timezone.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/icu_formatter_runtime.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

using detail::MakeBoxed;

[[noreturn]] void IcuFailure(const std::exception& error) {
  if (dynamic_cast<const DexVmError*>(&error)) throw;
  if (dynamic_cast<const std::bad_alloc*>(&error))
    throw VmJavaThrow{"Ljava/lang/OutOfMemoryError;", error.what()};
  if (dynamic_cast<const std::domain_error*>(&error))
    throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", error.what()};
  if (dynamic_cast<const std::invalid_argument*>(&error))
    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", error.what()};
  throw VmJavaThrow{"Ljava/lang/IllegalStateException;", error.what()};
}

[[nodiscard]] VmObjectRef StringArray(Interpreter& vm,
                                      const std::vector<std::u16string>& values) {
  const auto array_class = vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
  const auto string_class = vm.Linker().ResolveDescriptor("Ljava/lang/String;");
  const auto array = vm.Model().NewObjectArray(
      array_class, string_class, static_cast<JniSize>(values.size()));
  const std::array roots{array};
  const auto scope = vm.ProtectReferences(roots);
  for (std::size_t index = 0; index < values.size(); ++index) {
    vm.Model().SetObjectElement(array, static_cast<JniSize>(index),
                                vm.Model().NewString(values[index]));
  }
  return array;
}

[[nodiscard]] VmObjectRef CharArray(Interpreter& vm,
                                    const std::u16string& value) {
  const auto array = vm.Model().NewPrimitiveArray(
      vm.Linker().ResolveDescriptor("[C"), JniPrimitiveKind::character,
      static_cast<JniSize>(value.size()));
  for (std::size_t index = 0; index < value.size(); ++index) {
    vm.Model().SetPrimitiveElement(array, static_cast<JniSize>(index),
                                   value[index]);
  }
  return array;
}

struct LocaleDataFields final {
  IntrinsicFieldHandle nan;
  IntrinsicFieldHandle am_pm;
  IntrinsicFieldHandle currency_pattern;
  IntrinsicFieldHandle currency_symbol;
  IntrinsicFieldHandle decimal_separator;
  IntrinsicFieldHandle eras;
  IntrinsicFieldHandle exponent_separator;
  IntrinsicFieldHandle first_day;
  IntrinsicFieldHandle full_date;
  IntrinsicFieldHandle full_time;
  IntrinsicFieldHandle grouping_separator;
  IntrinsicFieldHandle infinity;
  IntrinsicFieldHandle integer_pattern;
  IntrinsicFieldHandle international_currency_symbol;
  IntrinsicFieldHandle long_date;
  IntrinsicFieldHandle long_months;
  IntrinsicFieldHandle long_standalone_months;
  IntrinsicFieldHandle long_standalone_weekdays;
  IntrinsicFieldHandle long_time;
  IntrinsicFieldHandle long_weekdays;
  IntrinsicFieldHandle medium_date;
  IntrinsicFieldHandle medium_time;
  IntrinsicFieldHandle minimal_days;
  IntrinsicFieldHandle minus_sign;
  IntrinsicFieldHandle monetary_separator;
  IntrinsicFieldHandle number_pattern;
  IntrinsicFieldHandle pattern_separator;
  IntrinsicFieldHandle per_mill;
  IntrinsicFieldHandle percent;
  IntrinsicFieldHandle percent_pattern;
  IntrinsicFieldHandle short_date;
  IntrinsicFieldHandle short_date4;
  IntrinsicFieldHandle short_months;
  IntrinsicFieldHandle short_standalone_months;
  IntrinsicFieldHandle short_standalone_weekdays;
  IntrinsicFieldHandle short_time;
  IntrinsicFieldHandle short_weekdays;
  IntrinsicFieldHandle time12;
  IntrinsicFieldHandle time24;
  IntrinsicFieldHandle tiny_months;
  IntrinsicFieldHandle tiny_standalone_months;
  IntrinsicFieldHandle tiny_standalone_weekdays;
  IntrinsicFieldHandle tiny_weekdays;
  IntrinsicFieldHandle today;
  IntrinsicFieldHandle tomorrow;
  IntrinsicFieldHandle yesterday;
  IntrinsicFieldHandle zero_digit;
};

[[nodiscard]] LocaleDataFields BindLocaleDataFields(
    IntrinsicClassBuilder& builder) {
  const auto ref = [&builder](const char* name, const char* descriptor) {
    return builder.BoundInstanceField(name, descriptor, kAccPublic);
  };
  return {
      ref("NaN", "Ljava/lang/String;"), ref("amPm", "[Ljava/lang/String;"),
      ref("currencyPattern", "Ljava/lang/String;"),
      ref("currencySymbol", "Ljava/lang/String;"),
      ref("decimalSeparator", "C"), ref("eras", "[Ljava/lang/String;"),
      ref("exponentSeparator", "Ljava/lang/String;"),
      ref("firstDayOfWeek", "Ljava/lang/Integer;"),
      ref("fullDateFormat", "Ljava/lang/String;"),
      ref("fullTimeFormat", "Ljava/lang/String;"),
      ref("groupingSeparator", "C"), ref("infinity", "Ljava/lang/String;"),
      ref("integerPattern", "Ljava/lang/String;"),
      ref("internationalCurrencySymbol", "Ljava/lang/String;"),
      ref("longDateFormat", "Ljava/lang/String;"),
      ref("longMonthNames", "[Ljava/lang/String;"),
      ref("longStandAloneMonthNames", "[Ljava/lang/String;"),
      ref("longStandAloneWeekdayNames", "[Ljava/lang/String;"),
      ref("longTimeFormat", "Ljava/lang/String;"),
      ref("longWeekdayNames", "[Ljava/lang/String;"),
      ref("mediumDateFormat", "Ljava/lang/String;"),
      ref("mediumTimeFormat", "Ljava/lang/String;"),
      ref("minimalDaysInFirstWeek", "Ljava/lang/Integer;"),
      ref("minusSign", "C"), ref("monetarySeparator", "C"),
      ref("numberPattern", "Ljava/lang/String;"), ref("patternSeparator", "C"),
      ref("perMill", "C"), ref("percent", "C"),
      ref("percentPattern", "Ljava/lang/String;"),
      ref("shortDateFormat", "Ljava/lang/String;"),
      ref("shortDateFormat4", "Ljava/lang/String;"),
      ref("shortMonthNames", "[Ljava/lang/String;"),
      ref("shortStandAloneMonthNames", "[Ljava/lang/String;"),
      ref("shortStandAloneWeekdayNames", "[Ljava/lang/String;"),
      ref("shortTimeFormat", "Ljava/lang/String;"),
      ref("shortWeekdayNames", "[Ljava/lang/String;"),
      ref("timeFormat12", "Ljava/lang/String;"),
      ref("timeFormat24", "Ljava/lang/String;"),
      ref("tinyMonthNames", "[Ljava/lang/String;"),
      ref("tinyStandAloneMonthNames", "[Ljava/lang/String;"),
      ref("tinyStandAloneWeekdayNames", "[Ljava/lang/String;"),
      ref("tinyWeekdayNames", "[Ljava/lang/String;"),
      ref("today", "Ljava/lang/String;"), ref("tomorrow", "Ljava/lang/String;"),
      ref("yesterday", "Ljava/lang/String;"), ref("zeroDigit", "C")};
}

void RequireLocale(const std::string& locale) {
  if (!(locale.empty() || locale == "en" || locale == "en_US" ||
        locale == "zh" || locale == "zh_CN"))
    throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                      "unsupported locale data: " + locale};
  InitializePinnedIcu();
}

using Resource = std::unique_ptr<UResourceBundle, decltype(&ures_close)>;
Resource ResourceChild(UResourceBundle* parent, const char* key) {
  UErrorCode status = U_ZERO_ERROR;
  Resource result(ures_getByKey(parent, key, nullptr, &status), ures_close);
  CheckIcu(status);
  return result;
}

std::u16string CurrencyCode(const std::string& country) {
  InitializePinnedIcu();
  if (country.empty()) return {};
  UErrorCode status = U_ZERO_ERROR;
  // libcore uses the fixed supplemental currency map, not a time-dependent lookup.
  Resource supplemental(ures_openDirect(U_ICUDATA_NAME "-curr", "supplementalData", &status), ures_close);
  CheckIcu(status);
  auto map = ResourceChild(supplemental.get(), "CurrencyMap");
  Resource region(ures_getByKey(map.get(), country.c_str(), nullptr, &status), ures_close);
  if (status == U_MISSING_RESOURCE_ERROR) return {};
  CheckIcu(status);
  Resource currency(ures_getByIndex(region.get(), 0, nullptr, &status), ures_close);
  if (status == U_MISSING_RESOURCE_ERROR) return u"XXX";
  CheckIcu(status);
  Resource end(ures_getByKey(currency.get(), "to", nullptr, &status), ures_close);
  if (U_SUCCESS(status)) return {};
  if (status != U_MISSING_RESOURCE_ERROR) CheckIcu(status);
  status = U_ZERO_ERROR;
  int32_t length{};
  const auto* code = ures_getStringByKey(currency.get(), "id", &length, &status);
  if (status == U_MISSING_RESOURCE_ERROR) return u"XXX";
  CheckIcu(status);
  return FromIcu(icu::UnicodeString(code, length));
}

std::u16string CurrencyName(const std::string& locale,
                            const std::u16string& code, UCurrNameStyle style) {
  RequireLocale(locale);
  auto currency = IcuString(code);
  UErrorCode status = U_ZERO_ERROR;
  UBool choice = false;
  int32_t length{};
  const auto* name = ucurr_getName(currency.getTerminatedBuffer(), locale.c_str(),
                                  style, &choice, &length, &status);
  // Match libcore's distinction between a missing currency and locale fallback.
  if (status == U_USING_DEFAULT_WARNING) {
    if (style == UCURR_LONG_NAME) return code;
    if (!ucurr_isAvailable(currency.getTerminatedBuffer(), U_DATE_MIN, U_DATE_MAX, &status))
      return {};
  }
  CheckIcu(status);
  return length ? FromIcu(icu::UnicodeString(name, length)) : std::u16string{};
}

void PopulateLocaleData(IntrinsicContext& context,
                        const LocaleDataFields& fields,
                        const std::string& locale_id,
                        const VmObjectRef target) {
  RequireLocale(locale_id);
  IntrinsicCall call(context);
  UErrorCode status = U_ZERO_ERROR;
  const auto locale = icu::Locale::createFromName(locale_id.c_str());
  const auto set_string = [&](IntrinsicFieldHandle field, const std::u16string& value) {
    call.SetRef(field, target, call.Vm().Model().NewString(value));
  };
  const auto set_array = [&](IntrinsicFieldHandle field, const icu::UnicodeString* strings, int32_t count) {
    std::vector<std::u16string> values;
    for (int32_t i = 0; i < count; ++i) values.push_back(FromIcu(strings[i]));
    call.SetRef(field, target, StringArray(call.Vm(), values));
  };
  Resource root(ures_open(nullptr, locale_id.c_str(), &status), ures_close);
  CheckIcu(status);
  auto calendars = ResourceChild(root.get(), "calendar");
  auto gregorian = ResourceChild(calendars.get(), "gregorian");
  auto patterns = ResourceChild(gregorian.get(), "DateTimePatterns");
  const std::array pattern_fields{fields.full_time, fields.long_time, fields.medium_time,
      fields.short_time, fields.full_date, fields.long_date, fields.medium_date, fields.short_date};
  for (int32_t i = 0; i < 8; ++i) {
    Resource item(ures_getByIndex(patterns.get(), i, nullptr, &status), ures_close);
    CheckIcu(status);
    int32_t length{};
    const UChar* value = ures_getType(item.get()) == URES_ARRAY
        ? ures_getStringByIndex(item.get(), 0, &length, &status)
        : ures_getString(item.get(), &length, &status);
    CheckIcu(status);
    set_string(pattern_fields[static_cast<std::size_t>(i)], FromIcu(icu::UnicodeString(value, length)));
  }
  auto resource_fields = ResourceChild(root.get(), "fields");
  auto day = ResourceChild(resource_fields.get(), "day");
  auto relative = ResourceChild(day.get(), "relative");
  for (const auto& [key, field] : std::array{
           std::pair{"-1", fields.yesterday}, std::pair{"0", fields.today}, std::pair{"1", fields.tomorrow}}) {
    int32_t length{};
    const UChar* value = ures_getStringByKey(relative.get(), key, &length, &status);
    CheckIcu(status);
    set_string(field, FromIcu(icu::UnicodeString(value, length)));
  }
  // An explicit GMT calendar avoids ICU's default-zone/host-time lookup.
  std::unique_ptr<icu::Calendar> calendar(icu::Calendar::createInstance(
      *icu::TimeZone::getGMT(), locale, status));
  CheckIcu(status);
  call.SetRef(fields.first_day, target, MakeBoxed(context, "Ljava/lang/Integer;",
      calendar->getFirstDayOfWeek(), false).ref);
  call.SetRef(fields.minimal_days, target, MakeBoxed(context, "Ljava/lang/Integer;",
      calendar->getMinimalDaysInFirstWeek(), false).ref);
  icu::DateFormatSymbols dates(locale, status);
  CheckIcu(status);
  int32_t count{};
  const auto* values = dates.getAmPmStrings(count);
  set_array(fields.am_pm, values, count);
  values = dates.getEras(count);
  set_array(fields.eras, values, count);
  using D = icu::DateFormatSymbols;
  const std::array month_fields{fields.long_months, fields.short_months, fields.tiny_months,
      fields.long_standalone_months, fields.short_standalone_months, fields.tiny_standalone_months};
  const std::array weekday_fields{fields.long_weekdays, fields.short_weekdays, fields.tiny_weekdays,
      fields.long_standalone_weekdays, fields.short_standalone_weekdays, fields.tiny_standalone_weekdays};
  const std::array widths{D::WIDE, D::ABBREVIATED, D::NARROW};
  for (std::size_t i = 0; i < month_fields.size(); ++i) {
    const auto usage = i < 3 ? D::FORMAT : D::STANDALONE;
    values = dates.getMonths(count, usage, widths[i % 3]);
    set_array(month_fields[i], values, count);
    values = dates.getWeekdays(count, usage, widths[i % 3]);
    set_array(weekday_fields[i], values, count);
  }
  for (const auto& [style, field] : std::array{
           std::pair{UNUM_DECIMAL, fields.number_pattern},
           std::pair{UNUM_CURRENCY, fields.currency_pattern},
           std::pair{UNUM_PERCENT, fields.percent_pattern}}) {
    std::unique_ptr<icu::NumberFormat> number(icu::NumberFormat::createInstance(locale, style, status));
    CheckIcu(status);
    icu::UnicodeString pattern;
    static_cast<icu::DecimalFormat*>(number.get())->toPattern(pattern);
    set_string(field, FromIcu(pattern));
  }
  icu::DecimalFormatSymbols numbers(locale, status);
  CheckIcu(status);
  using N = icu::DecimalFormatSymbols;
  for (const auto& [field, symbol] : std::array{
           std::pair{fields.zero_digit, N::kZeroDigitSymbol},
           std::pair{fields.decimal_separator, N::kDecimalSeparatorSymbol},
           std::pair{fields.grouping_separator, N::kGroupingSeparatorSymbol},
           std::pair{fields.pattern_separator, N::kPatternSeparatorSymbol},
           std::pair{fields.percent, N::kPercentSymbol},
           std::pair{fields.per_mill, N::kPerMillSymbol},
           std::pair{fields.monetary_separator, N::kMonetarySeparatorSymbol},
           std::pair{fields.minus_sign, N::kMinusSignSymbol}}) {
    call.SetInt(field, target, numbers.getSymbol(symbol).charAt(0));
  }
  set_string(fields.exponent_separator, FromIcu(numbers.getSymbol(N::kExponentialSymbol)));
  set_string(fields.infinity, FromIcu(numbers.getSymbol(N::kInfinitySymbol)));
  set_string(fields.nan, FromIcu(numbers.getSymbol(N::kNaNSymbol)));
  auto code = CurrencyCode(locale.getCountry());
  auto symbol = code.empty() ? std::u16string{} : CurrencyName(locale_id, code, UCURR_SYMBOL_NAME);
  set_string(fields.international_currency_symbol, code.empty() ? u"XXX" : code);
  set_string(fields.currency_symbol, symbol.empty() ? u"¤" : symbol);
  // integerPattern, shortDateFormat4 and hm/Hm patterns are populated by BootDex LocaleData.
}

IntrinsicClassDecl DeclareIcu(const LocaleDataFields fields) {
  auto builder = IntrinsicClassBuilder::Class("Llibcore/icu/ICU;");
  builder.StaticMethod(
      "getBestDateTimePatternNative",
      "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
      [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        static_cast<void>(call.NonNullRef(0, "skeleton"));
        const auto locale = call.Vm().StringUtf8(call.NonNullRef(1, "locale"));
        RequireLocale(locale);
        UErrorCode status = U_ZERO_ERROR;
        std::unique_ptr<icu::DateTimePatternGenerator> generator(
            icu::DateTimePatternGenerator::createInstance(icu::Locale::createFromName(locale.c_str()), status));
        CheckIcu(status);
        const auto pattern = generator->getBestPattern(IcuString(
            call.Vm().Model().StringValue(call.Ref(0))), status);
        CheckIcu(status);
        return VmValue::Ref(call.Vm().Model().NewString(FromIcu(pattern)));
      }, kAccPrivate | kAccNative);
  builder.StaticMethod("getCurrencyCode", "(Ljava/lang/String;)Ljava/lang/String;",
      [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto code = CurrencyCode(call.Vm().StringUtf8(call.NonNullRef(0, "country")));
        return VmValue::Ref(code.empty() ? VmObjectRef{} : call.Vm().Model().NewString(code));
      }, kAccPublic | kAccNative);
  for (const auto& [method, style] : std::array{
           std::pair{"getCurrencyDisplayName", UCURR_LONG_NAME},
           std::pair{"getCurrencySymbol", UCURR_SYMBOL_NAME}}) {
    builder.StaticMethod(method, "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        [style](IntrinsicContext& context) {
          IntrinsicCall call(context);
          const auto name = CurrencyName(call.Vm().StringUtf8(call.NonNullRef(0, "locale")),
              call.Vm().Model().StringValue(call.NonNullRef(1, "currencyCode")), style);
          return VmValue::Ref(name.empty() ? VmObjectRef{} : call.Vm().Model().NewString(name));
        }, kAccPublic | kAccNative);
  }
  builder.StaticMethod("getCurrencyFractionDigits", "(Ljava/lang/String;)I",
      [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        InitializePinnedIcu();
        auto code = IcuString(call.Vm().Model().StringValue(call.NonNullRef(0, "currencyCode")));
        if (code == icu::UnicodeString("XXX")) return VmValue::Int(-1);
        UErrorCode status = U_ZERO_ERROR;
        auto digits = ucurr_getDefaultFractionDigits(code.getTerminatedBuffer(), &status);
        CheckIcu(status);
        return VmValue::Int(digits);
      }, kAccPublic | kAccNative);
  builder.StaticMethod("initLocaleDataNative",
      "(Ljava/lang/String;Llibcore/icu/LocaleData;)Z",
      [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto locale = call.Vm().StringUtf8(call.NonNullRef(0, "locale"));
        const auto data = call.NonNullRef(1, "localeData");
        if (!(locale.empty() || locale == "en" || locale == "en_US" ||
              locale == "zh" || locale == "zh_CN")) {
          throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                            "unsupported locale data: " + locale};
        }
        PopulateLocaleData(context, fields, locale, data);
        return VmValue::Int(1);
      }, kAccStatic | kAccNative);
  // Remaining ICU natives are deliberately registered as unsupported below.
  constexpr std::pair<const char*, const char*> failures[]{
      {"addLikelySubtags", "(Ljava/lang/String;)Ljava/lang/String;"},
      {"getAvailableBreakIteratorLocalesNative", "()[Ljava/lang/String;"},
      {"getAvailableCalendarLocalesNative", "()[Ljava/lang/String;"},
      {"getAvailableCollatorLocalesNative", "()[Ljava/lang/String;"},
      {"getAvailableCurrencyCodes", "()[Ljava/lang/String;"},
      {"getAvailableDateFormatLocalesNative", "()[Ljava/lang/String;"},
      {"getAvailableLocalesNative", "()[Ljava/lang/String;"},
      {"getAvailableNumberFormatLocalesNative", "()[Ljava/lang/String;"},
      {"getCldrVersion", "()Ljava/lang/String;"},
      {"getDisplayCountryNative", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"},
      {"getDisplayLanguageNative", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"},
      {"getDisplayScriptNative", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"},
      {"getDisplayVariantNative", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"},
      {"getISO3CountryNative", "(Ljava/lang/String;)Ljava/lang/String;"},
      {"getISO3LanguageNative", "(Ljava/lang/String;)Ljava/lang/String;"},
      {"getISOCountriesNative", "()[Ljava/lang/String;"},
      {"getISOLanguagesNative", "()[Ljava/lang/String;"},
      {"getIcuVersion", "()Ljava/lang/String;"},
      {"getScript", "(Ljava/lang/String;)Ljava/lang/String;"},
      {"getUnicodeVersion", "()Ljava/lang/String;"},
      {"languageTagForLocale", "(Ljava/lang/String;)Ljava/lang/String;"},
      {"localeForLanguageTag", "(Ljava/lang/String;Z)Ljava/lang/String;"},
      {"toLowerCase", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"},
      {"toUpperCase", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"}};
  for (const auto& [name, descriptor] : failures) {
    builder.UnimplementedStatic(name, descriptor, kAccPublic | kAccNative);
  }
  return std::move(builder).Build();
}

struct FieldPositionIteratorFields final {
  IntrinsicFieldHandle data;
};

struct ParsePositionFields final {
  IntrinsicFieldHandle current;
  IntrinsicFieldHandle error;
};

IntrinsicClassDecl DeclareNativeDecimalFormat(
    const FieldPositionIteratorFields fpi_fields,
    const ParsePositionFields parse_fields) {
  auto builder = IntrinsicClassBuilder::Class("Llibcore/icu/NativeDecimalFormat;");
  static_cast<void>(builder.BoundInstanceField("address", "J", kAccPrivate));
  builder.StaticMethod("open",
      "(Ljava/lang/String;Ljava/lang/String;CCLjava/lang/String;CLjava/lang/String;Ljava/lang/String;CCLjava/lang/String;CCCC)J",
      [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        IcuFormatterRuntime::Symbols symbols;
        symbols.currency_symbol = call.Vm().Model().StringValue(call.NonNullRef(1, "currencySymbol"));
        symbols.decimal_separator = static_cast<char16_t>(call.Int(2));
        symbols.digit = static_cast<char16_t>(call.Int(3));
        symbols.exponent_separator = call.Vm().Model().StringValue(call.NonNullRef(4, "exponentSeparator"));
        symbols.grouping_separator = static_cast<char16_t>(call.Int(5));
        symbols.infinity = call.Vm().Model().StringValue(call.NonNullRef(6, "infinity"));
        symbols.international_currency_symbol = call.Vm().Model().StringValue(call.NonNullRef(7, "internationalCurrencySymbol"));
        symbols.minus_sign = static_cast<char16_t>(call.Int(8));
        symbols.monetary_separator = static_cast<char16_t>(call.Int(9));
        symbols.nan = call.Vm().Model().StringValue(call.NonNullRef(10, "NaN"));
        symbols.pattern_separator = static_cast<char16_t>(call.Int(11));
        symbols.percent = static_cast<char16_t>(call.Int(12));
        symbols.per_mill = static_cast<char16_t>(call.Int(13));
        symbols.zero_digit = static_cast<char16_t>(call.Int(14));
        try {
          return VmValue::Long(static_cast<std::int64_t>(call.Vm().IcuFormatters().Open(
              call.Vm().Model().StringValue(call.NonNullRef(0, "pattern")),
              std::move(symbols))));
        } catch (const std::exception& error) { IcuFailure(error); }
      }, kAccPrivate | kAccNative);
  builder.StaticMethod("cloneImpl", "(J)J", [](IntrinsicContext& context) {
    try { return VmValue::Long(static_cast<std::int64_t>(context.vm.IcuFormatters().Clone(
        static_cast<std::uint64_t>(context.arguments[0].AsLong())))); }
    catch (const std::exception& error) { IcuFailure(error); }
  }, kAccPrivate | kAccNative);
  builder.StaticMethod("close", "(J)V", [](IntrinsicContext& context) {
    try { context.vm.IcuFormatters().Close(
        static_cast<std::uint64_t>(context.arguments[0].AsLong())); }
    catch (const std::exception& error) { IcuFailure(error); }
    return VmValue::Void();
  }, kAccPrivate | kAccNative);
  builder.StaticMethod("applyPatternImpl", "(JZLjava/lang/String;)V",
      [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        try { call.Vm().IcuFormatters().ApplyPattern(
            static_cast<std::uint64_t>(call.Long(0)), call.Vm().Model().StringValue(
                call.NonNullRef(2, "pattern")), call.Int(1) != 0); }
        catch (const std::exception& error) { IcuFailure(error); }
        return VmValue::Void();
      }, kAccPrivate | kAccNative);
  builder.StaticMethod("toPatternImpl", "(JZ)Ljava/lang/String;",
      [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        try { return VmValue::Ref(call.Vm().Model().NewString(
            call.Vm().IcuFormatters().Pattern(
                static_cast<std::uint64_t>(call.Long(0)), call.Int(1) != 0))); }
        catch (const std::exception& error) { IcuFailure(error); }
      }, kAccPrivate | kAccNative);
  builder.StaticMethod("formatLong",
      "(JJLlibcore/icu/NativeDecimalFormat$FieldPositionIterator;)[C",
      [fpi_fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        try {
          const auto result = call.Vm().IcuFormatters().FormatLong(
              static_cast<std::uint64_t>(call.Long(0)), call.Long(1));
          const auto iterator = call.Ref(2);
          if (iterator.IsValid()) {
            const auto data = call.Vm().Model().NewPrimitiveArray(
                call.Vm().Linker().ResolveDescriptor("[I"),
                JniPrimitiveKind::integer, static_cast<JniSize>(result.fields.size()));
            for (std::size_t index = 0; index < result.fields.size(); ++index)
              call.Vm().Model().SetPrimitiveElement(data, static_cast<JniSize>(index),
                  static_cast<std::uint32_t>(result.fields[index]));
            call.SetRef(fpi_fields.data, iterator, data);
          }
          return VmValue::Ref(CharArray(call.Vm(), result.text));
        } catch (const std::exception& error) { IcuFailure(error); }
      }, kAccPrivate | kAccNative);
  builder.StaticMethod("getAttribute", "(JI)I", [](IntrinsicContext& context) {
    IntrinsicCall call(context);
    try { return VmValue::Int(call.Vm().IcuFormatters().GetAttribute(
        static_cast<std::uint64_t>(call.Long(0)), call.Int(1))); }
    catch (const std::exception& error) { IcuFailure(error); }
  }, kAccPrivate | kAccNative);
  builder.StaticMethod("setAttribute", "(JII)V", [](IntrinsicContext& context) {
    IntrinsicCall call(context);
    try { call.Vm().IcuFormatters().SetAttribute(
        static_cast<std::uint64_t>(call.Long(0)), call.Int(1), call.Int(2)); }
    catch (const std::exception& error) { IcuFailure(error); }
    return VmValue::Void();
  }, kAccPrivate | kAccNative);
  builder.StaticMethod("getTextAttribute", "(JI)Ljava/lang/String;",
      [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        try { return VmValue::Ref(call.Vm().Model().NewString(
            call.Vm().IcuFormatters().GetTextAttribute(
                static_cast<std::uint64_t>(call.Long(0)), call.Int(1)))); }
        catch (const std::exception& error) { IcuFailure(error); }
      }, kAccPrivate | kAccNative);
  builder.StaticMethod("setTextAttribute", "(JILjava/lang/String;)V",
      [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        try { call.Vm().IcuFormatters().SetTextAttribute(
            static_cast<std::uint64_t>(call.Long(0)), call.Int(1),
            call.Vm().Model().StringValue(call.NonNullRef(2, "value"))); }
        catch (const std::exception& error) { IcuFailure(error); }
        return VmValue::Void();
      }, kAccPrivate | kAccNative);
  builder.StaticMethod("setSymbol", "(JILjava/lang/String;)V",
      [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        try { call.Vm().IcuFormatters().SetSymbol(
            static_cast<std::uint64_t>(call.Long(0)), call.Int(1),
            call.Vm().Model().StringValue(call.NonNullRef(2, "value"))); }
        catch (const std::exception& error) { IcuFailure(error); }
        return VmValue::Void();
      }, kAccPrivate | kAccNative);
  builder.StaticMethod("setRoundingMode", "(JID)V",
      [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        try { call.Vm().IcuFormatters().SetRoundingMode(
            static_cast<std::uint64_t>(call.Long(0)), call.Int(1), call.Double(2)); }
        catch (const std::exception& error) { IcuFailure(error); }
        return VmValue::Void();
      }, kAccPrivate | kAccNative);
  builder.StaticMethod("setDecimalFormatSymbols",
      "(JLjava/lang/String;CCLjava/lang/String;CLjava/lang/String;Ljava/lang/String;CCLjava/lang/String;CCCC)V",
      [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        IcuFormatterRuntime::Symbols symbols;
        symbols.currency_symbol = call.Vm().Model().StringValue(call.NonNullRef(1, "currencySymbol"));
        symbols.decimal_separator = static_cast<char16_t>(call.Int(2));
        symbols.digit = static_cast<char16_t>(call.Int(3));
        symbols.exponent_separator = call.Vm().Model().StringValue(call.NonNullRef(4, "exponentSeparator"));
        symbols.grouping_separator = static_cast<char16_t>(call.Int(5));
        symbols.infinity = call.Vm().Model().StringValue(call.NonNullRef(6, "infinity"));
        symbols.international_currency_symbol = call.Vm().Model().StringValue(call.NonNullRef(7, "internationalCurrencySymbol"));
        symbols.minus_sign = static_cast<char16_t>(call.Int(8));
        symbols.monetary_separator = static_cast<char16_t>(call.Int(9));
        symbols.nan = call.Vm().Model().StringValue(call.NonNullRef(10, "NaN"));
        symbols.pattern_separator = static_cast<char16_t>(call.Int(11));
        symbols.percent = static_cast<char16_t>(call.Int(12));
        symbols.per_mill = static_cast<char16_t>(call.Int(13));
        symbols.zero_digit = static_cast<char16_t>(call.Int(14));
        try { call.Vm().IcuFormatters().SetSymbols(
            static_cast<std::uint64_t>(call.Long(0)), std::move(symbols)); }
        catch (const std::exception& error) { IcuFailure(error); }
        return VmValue::Void();
      }, kAccPrivate | kAccNative);
  builder.UnimplementedStatic("formatDigitList",
      "(JLjava/lang/String;Llibcore/icu/NativeDecimalFormat$FieldPositionIterator;)[C",
      kAccPrivate | kAccNative);
  builder.UnimplementedStatic("formatDouble",
      "(JDLlibcore/icu/NativeDecimalFormat$FieldPositionIterator;)[C",
      kAccPrivate | kAccNative);
  builder.StaticMethod("parse",
      "(JLjava/lang/String;Ljava/text/ParsePosition;Z)Ljava/lang/Number;",
      [parse_fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        if (call.Int(3) != 0) {
          throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                            "BigDecimal parsing is outside the DVM-102 closure"};
        }
        const auto position = call.NonNullRef(2, "position");
        const auto start = call.GetInt(parse_fields.current, position);
        const auto text = call.Vm().Model().StringValue(call.NonNullRef(1, "text"));
        if (start < 0 || static_cast<std::size_t>(start) > text.size())
          return VmValue::Ref(VmObjectRef{});
        try {
          std::int32_t error_index = start;
          const auto parsed = call.Vm().IcuFormatters().ParseInteger(
              static_cast<std::uint64_t>(call.Long(0)),
              text, static_cast<std::size_t>(start), &error_index);
          if (!parsed.has_value()) {
            call.SetInt(parse_fields.error, position, error_index);
            return VmValue::Ref(VmObjectRef{});
          }
          call.SetInt(parse_fields.current, position,
                      static_cast<std::int32_t>(parsed->end));
          return MakeBoxed(context, "Ljava/lang/Long;",
                           static_cast<std::uint64_t>(parsed->value), true);
        } catch (const std::exception& error) { IcuFailure(error); }
      }, kAccPrivate | kAccNative);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareTimeZoneNames() {
  auto builder = IntrinsicClassBuilder::Class("Llibcore/icu/TimeZoneNames;");
  builder.StaticMethod("fillZoneStrings", "(Ljava/lang/String;[[Ljava/lang/String;)V",
      [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto locale_name = call.Vm().StringUtf8(call.NonNullRef(0, "locale"));
        RequireLocale(locale_name);
        const auto locale = icu::Locale::createFromName(locale_name.c_str());
        const auto rows = call.NonNullRef(1, "zoneStrings");
        const auto length = call.Vm().Model().ArrayLength(rows);
        for (JniSize index = 0; index < length; ++index) {
          const auto row = call.Vm().Model().GetObjectElement(rows, index);
          if (!row.IsValid() || call.Vm().Model().ArrayLength(row) < 5)
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid zone strings row"};
          const auto id = call.Vm().Model().StringValue(call.Vm().Model().GetObjectElement(row, 0));
          if (id != u"GMT" && id != u"UTC")
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "named time-zone database is not packaged"};
          std::unique_ptr<icu::TimeZone> zone(icu::TimeZone::createTimeZone(IcuString(id)));
          for (int32_t column = 1; column < 5; ++column) {
            icu::UnicodeString name;
            zone->getDisplayName(column >= 3, column % 2 ? icu::TimeZone::LONG : icu::TimeZone::SHORT,
                                  locale, name);
            call.Vm().Model().SetObjectElement(row, column, call.Vm().Model().NewString(FromIcu(name)));
          }
        }
        return VmValue::Void();
      }, kAccPrivate | kAccNative);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareTimeZoneBoundary(const CoreIntrinsicServices& services) {
  auto builder = IntrinsicClassBuilder::Class("Ljava/util/TimeZone;");
  const auto gmt = builder.BoundStaticField(
      "GMT", "Ljava/util/TimeZone;", kAccPrivate | kAccStatic | kAccFinal);
  const auto utc = builder.BoundStaticField(
      "UTC", "Ljava/util/TimeZone;", kAccPrivate | kAccStatic | kAccFinal);
  const auto default_zone = builder.BoundStaticField(
      "defaultTimeZone", "Ljava/util/TimeZone;", kAccPrivate | kAccStatic);
  const auto ids = [](IntrinsicContext& context) {
    return VmValue::Ref(StringArray(context.vm, {u"GMT", u"UTC"}));
  };
  builder.StaticMethod("getAvailableIDs", "()[Ljava/lang/String;", ids,
                       kAccPublic | kAccSynchronized);
  builder.StaticMethod("getAvailableIDs", "(I)[Ljava/lang/String;",
      [ids](IntrinsicContext& context) {
        return context.arguments[0].AsInt() == 0
                   ? ids(context)
                   : VmValue::Ref(StringArray(context.vm, {}));
      }, kAccPublic | kAccSynchronized);
  builder.StaticMethod("getDefault", "()Ljava/util/TimeZone;",
      [default_zone, initial_zone = services.default_timezone](IntrinsicContext& context) {
        IntrinsicCall call(context);
        auto zone = call.GetRef(default_zone);
        if (!zone.IsValid()) {
          const auto owner = call.Vm().Linker().ResolveDescriptor("Ljava/util/TimeZone;");
          const auto method = call.Vm().Linker().FindDirectMethod(owner, "getTimeZone",
              "(Ljava/lang/String;)Ljava/util/TimeZone;");
          if (!method.has_value())
            throw DexVmError{DexVmErrorReason::invalid_member, "TimeZone.getTimeZone is missing"};
          const std::array args{VmValue::Ref(call.Vm().NewStringUtf8(initial_zone))};
          const auto outcome = call.Vm().Call(*method, args);
          if (outcome.exception.IsValid()) {
            call.Vm().SetPendingException(outcome.exception);
            return VmValue::Ref(VmObjectRef{});
          }
          zone = outcome.value.ref;
          call.SetRef(default_zone, zone);
        }
        return VmValue::Ref(call.Vm().CloneObject(zone));
      }, kAccPublic | kAccSynchronized);
  builder.StaticMethod("getTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;",
      [gmt, utc](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto id_ref = call.NonNullRef(0, "id");
        const auto id = call.Vm().StringUtf8(id_ref);
        if (id == "GMT") return VmValue::Ref(call.Vm().CloneObject(call.GetRef(gmt)));
        if (id == "UTC") return VmValue::Ref(call.Vm().CloneObject(call.GetRef(utc)));
        if (id.starts_with("GMT+") || id.starts_with("GMT-")) {
          const auto owner = call.Vm().Linker().ResolveDescriptor("Ljava/util/TimeZone;");
          const auto method = call.Vm().Linker().FindDirectMethod(
              owner, "getCustomTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;");
          if (!method.has_value()) {
            throw DexVmError{DexVmErrorReason::invalid_member,
                             "TimeZone.getCustomTimeZone is missing"};
          }
          const std::array arguments{VmValue::Ref(id_ref)};
          const auto outcome = call.Vm().Call(*method, arguments);
          if (outcome.exception.IsValid()) {
            call.Vm().SetPendingException(outcome.exception);
            return VmValue::Ref(VmObjectRef{});
          }
          return outcome.value.ref.IsValid()
              ? outcome.value
              : VmValue::Ref(call.Vm().CloneObject(call.GetRef(gmt)));
        }
        InitializePinnedIcu();
        std::unique_ptr<icu::TimeZone> known(icu::TimeZone::createTimeZone(
            IcuString(call.Vm().Model().StringValue(id_ref))));
        icu::UnicodeString known_id, unknown_id;
        known->getID(known_id);
        icu::TimeZone::getUnknown().getID(unknown_id);
        if (known_id == unknown_id) {
          return VmValue::Ref(call.Vm().CloneObject(call.GetRef(gmt)));
        }
        throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                          "named time-zone database is not packaged: " + id};
      }, kAccPublic | kAccSynchronized);
  return std::move(builder).Build();
}

}  // namespace

void AppendJavaIcu(std::vector<IntrinsicClassDecl>& catalog, const CoreIntrinsicServices& services) {
  const auto first = catalog.size();
  auto locale_data = IntrinsicClassBuilder::Class("Llibcore/icu/LocaleData;");
  const auto locale_fields = BindLocaleDataFields(locale_data);
  catalog.push_back(DeclareIcu(locale_fields));
  catalog.push_back(std::move(locale_data).Build());
  auto fpi = IntrinsicClassBuilder::Class(
      "Llibcore/icu/NativeDecimalFormat$FieldPositionIterator;");
  const FieldPositionIteratorFields fpi_fields{
      fpi.BoundInstanceField("data", "[I", kAccPrivate)};
  auto parse_position = IntrinsicClassBuilder::Class("Ljava/text/ParsePosition;");
  const ParsePositionFields parse_fields{
      parse_position.BoundInstanceField("currentPosition", "I", kAccPrivate),
      parse_position.BoundInstanceField("errorIndex", "I", kAccPrivate)};
  catalog.push_back(DeclareNativeDecimalFormat(fpi_fields, parse_fields));
  catalog.push_back(std::move(fpi).Build());
  catalog.push_back(std::move(parse_position).Build());
  catalog.push_back(DeclareTimeZoneNames());
  catalog.push_back(DeclareTimeZoneBoundary(services));
  for (std::size_t index = first; index < catalog.size(); ++index) {
    for (auto& method : catalog[index].methods) {
      if (!method.implementation) continue;
      method.implementation = [handler = std::move(method.implementation)](IntrinsicContext& context) {
        try { return handler(context); }
        catch (const std::exception& error) { IcuFailure(error); }
      };
    }
  }
}

}  // namespace ogplay::runtime::dexvm::intrinsics
