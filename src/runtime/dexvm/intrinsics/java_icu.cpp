#include "catalog.h"
#include "shared.h"

#include <array>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

VmObjectRef StringArray(Interpreter &vm,
                        const std::vector<std::u16string> &values) {
  const auto result = vm.Model().NewObjectArray(
      vm.Linker().ResolveDescriptor("[Ljava/lang/String;"),
      vm.Linker().ResolveDescriptor("Ljava/lang/String;"),
      static_cast<JniSize>(values.size()));
  const auto roots = vm.ProtectReferences(std::array{result});
  for (std::size_t i = 0; i < values.size(); ++i)
    vm.Model().SetObjectElement(result, static_cast<JniSize>(i),
                                vm.Model().NewString(values[i]));
  return result;
}

IntrinsicClassDecl IcuBoundary() {
  auto b = IntrinsicClassBuilder::Class("Llibcore/icu/ICU;");
  for (const auto &[name, descriptor, flags] : std::array{
           std::tuple{
               "getCurrencyDisplayName",
               "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
               kAccPublic | kAccStatic | kAccNative},
           std::tuple{"getISOCountriesNative", "()[Ljava/lang/String;",
                      kAccPrivate | kAccStatic | kAccNative},
           std::tuple{"getISOLanguagesNative", "()[Ljava/lang/String;",
                      kAccPrivate | kAccStatic | kAccNative},
           std::tuple{
               "toLowerCase",
               "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
               kAccPublic | kAccStatic | kAccNative},
           std::tuple{
               "toUpperCase",
               "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
               kAccPublic | kAccStatic | kAccNative}})
    b.GuestNativeStatic(name, descriptor, flags);
  b.StaticMethod(
      "getCurrencyCode", "(Ljava/lang/String;)Ljava/lang/String;",
      [](IntrinsicContext &c) {
        const auto country = c.vm.StringUtf8(c.arguments[0].ref);
        const auto code = country == "US"   ? "USD"
                          : country == "CN" ? "CNY"
                          : country == "GB" ? "GBP"
                          : country == "JP" ? "JPY"
                          : country == "CA" ? "CAD"
                          : country == "AU" ? "AUD"
                                            : nullptr;
        return VmValue::Ref(code ? c.vm.NewStringUtf8(code) : VmObjectRef{});
      },
      kAccPublic | kAccStatic | kAccNative);
  b.StaticMethod(
      "getCurrencySymbol",
      "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
      [](IntrinsicContext &c) {
        const auto locale = c.vm.StringUtf8(c.arguments[0].ref);
        const auto currency = c.vm.StringUtf8(c.arguments[1].ref);
        const auto symbol =
            currency == "USD" && locale == "en_US" ? "$"
            : currency == "CNY" && locale == "zh_CN" ? "¥"
            : currency == "GBP"                       ? "£"
            : currency == "JPY"                       ? "¥"
                                                      : currency.c_str();
        return VmValue::Ref(c.vm.NewStringUtf8(symbol));
      },
      kAccPublic | kAccStatic | kAccNative);
  b.StaticMethod(
      "getCurrencyFractionDigits", "(Ljava/lang/String;)I",
      [](IntrinsicContext &c) {
        const auto currency = c.vm.StringUtf8(c.arguments[0].ref);
        return VmValue::Int(currency == "JPY" ? 0 : currency == "XXX" ? -1 : 2);
      },
      kAccPublic | kAccStatic | kAccNative);
  b.StaticMethod(
      "getBestDateTimePatternNative",
      "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
      [](IntrinsicContext &c) {
        const auto skeleton = c.vm.StringUtf8(c.arguments[0].ref);
        return VmValue::Ref(
            c.vm.NewStringUtf8(skeleton == "Hm" ? "HH:mm" : "h:mm a"));
      },
      kAccPrivate | kAccStatic | kAccNative);
  b.StaticMethod(
      "initLocaleDataNative", "(Ljava/lang/String;Llibcore/icu/LocaleData;)Z",
      [](IntrinsicContext &c) {
        const auto locale = c.vm.StringUtf8(c.arguments[0].ref);
        if (!(locale.empty() || locale == "en" || locale == "en_US" ||
              locale == "zh" || locale == "zh_CN"))
          return VmValue::Int(0);
        const auto object = c.arguments[1].ref;
        const auto set_ref = [&](const char *name, const char *descriptor,
                                 VmObjectRef value) {
          const auto field = c.vm.Linker().FindFieldRecursive(
              c.vm.Model().ObjectClass(object), name, descriptor);
          if (!field)
            return;
          const auto &linked = c.vm.Linker().Field(*field);
          c.vm.Model().InstanceSlots(object)[linked.slot] = {value.Value(),
                                                             SlotTag::ref};
        };
        const auto set_string = [&](const char *name, const char *value) {
          set_ref(name, "Ljava/lang/String;", c.vm.NewStringUtf8(value));
        };
        const auto set_strings = [&](const char *name,
                                     std::vector<std::u16string> values) {
          set_ref(name, "[Ljava/lang/String;", StringArray(c.vm, values));
        };
        const auto set_char = [&](const char *name, char16_t value) {
          const auto field = c.vm.Linker().FindFieldRecursive(
              c.vm.Model().ObjectClass(object), name, "C");
          if (!field)
            return;
          const auto &linked = c.vm.Linker().Field(*field);
          c.vm.Model().InstanceSlots(object)[linked.slot] = {
              static_cast<std::uint64_t>(value), SlotTag::cat1};
        };
        set_strings("amPm", {u"AM", u"PM"});
        set_strings("eras", {u"BC", u"AD"});
        const std::vector<std::u16string> months{
            u"January",  u"February", u"March",  u"April",     u"May",
            u"June",     u"July",     u"August", u"September", u"October",
            u"November", u"December", u""};
        const std::vector<std::u16string> short_months{
            u"Jan", u"Feb", u"Mar", u"Apr", u"May", u"Jun", u"Jul",
            u"Aug", u"Sep", u"Oct", u"Nov", u"Dec", u""};
        const std::vector<std::u16string> weekdays{
            u"",          u"Sunday",   u"Monday", u"Tuesday",
            u"Wednesday", u"Thursday", u"Friday", u"Saturday"};
        const std::vector<std::u16string> short_weekdays{
            u"", u"Sun", u"Mon", u"Tue", u"Wed", u"Thu", u"Fri", u"Sat"};
        for (const auto *name : {"longMonthNames", "longStandAloneMonthNames"})
          set_strings(name, months);
        for (const auto *name : {"shortMonthNames", "shortStandAloneMonthNames",
                                 "tinyMonthNames", "tinyStandAloneMonthNames"})
          set_strings(name, short_months);
        for (const auto *name :
             {"longWeekdayNames", "longStandAloneWeekdayNames"})
          set_strings(name, weekdays);
        for (const auto *name :
             {"shortWeekdayNames", "shortStandAloneWeekdayNames",
              "tinyWeekdayNames", "tinyStandAloneWeekdayNames"})
          set_strings(name, short_weekdays);
        for (const auto &[name, value] :
             std::array{std::pair{"fullTimeFormat", "h:mm:ss a zzzz"},
                        std::pair{"longTimeFormat", "h:mm:ss a z"},
                        std::pair{"mediumTimeFormat", "h:mm:ss a"},
                        std::pair{"shortTimeFormat", "h:mm a"},
                        std::pair{"fullDateFormat", "EEEE, MMMM d, y"},
                        std::pair{"longDateFormat", "MMMM d, y"},
                        std::pair{"mediumDateFormat", "MMM d, y"},
                        std::pair{"shortDateFormat", "M/d/yy"},
                        std::pair{"numberPattern", "#,##0.###"},
                        std::pair{"currencyPattern", "¤#,##0.00"},
                        std::pair{"percentPattern", "#,##0%"},
                        std::pair{"exponentSeparator", "E"},
                        std::pair{"infinity", "∞"}, std::pair{"NaN", "NaN"},
                        std::pair{"currencySymbol", "$"},
                        std::pair{"internationalCurrencySymbol", "USD"}})
          set_string(name, value);
        set_char("zeroDigit", u'0');
        set_char("decimalSeparator", u'.');
        set_char("groupingSeparator", u',');
        set_char("patternSeparator", u';');
        set_char("percent", u'%');
        set_char("perMill", u'‰');
        set_char("monetarySeparator", u'.');
        set_char("minusSign", u'-');
        return VmValue::Int(1);
      },
      kAccStatic | kAccNative);
  constexpr std::pair<const char *, const char *> failures[]{
      {"addLikelySubtags", "(Ljava/lang/String;)Ljava/lang/String;"},
      {"getAvailableBreakIteratorLocalesNative", "()[Ljava/lang/String;"},
      {"getAvailableCalendarLocalesNative", "()[Ljava/lang/String;"},
      {"getAvailableCollatorLocalesNative", "()[Ljava/lang/String;"},
      {"getAvailableCurrencyCodes", "()[Ljava/lang/String;"},
      {"getAvailableDateFormatLocalesNative", "()[Ljava/lang/String;"},
      {"getAvailableLocalesNative", "()[Ljava/lang/String;"},
      {"getAvailableNumberFormatLocalesNative", "()[Ljava/lang/String;"},
      {"getCldrVersion", "()Ljava/lang/String;"},
      {"getDisplayCountryNative",
       "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"},
      {"getDisplayLanguageNative",
       "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"},
      {"getDisplayScriptNative",
       "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"},
      {"getDisplayVariantNative",
       "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"},
      {"getISO3CountryNative", "(Ljava/lang/String;)Ljava/lang/String;"},
      {"getISO3LanguageNative", "(Ljava/lang/String;)Ljava/lang/String;"},
      {"getIcuVersion", "()Ljava/lang/String;"},
      {"getScript", "(Ljava/lang/String;)Ljava/lang/String;"},
      {"getUnicodeVersion", "()Ljava/lang/String;"},
      {"languageTagForLocale", "(Ljava/lang/String;)Ljava/lang/String;"},
      {"localeForLanguageTag", "(Ljava/lang/String;Z)Ljava/lang/String;"}};
  for (const auto &[name, descriptor] : failures)
    b.UnimplementedStatic(name, descriptor,
                          kAccPublic | kAccStatic | kAccNative);
  return std::move(b).Build();
}

IntrinsicClassDecl DecimalBoundary() {
  auto b = IntrinsicClassBuilder::Class("Llibcore/icu/NativeDecimalFormat;");
  constexpr std::pair<const char *, const char *> methods[]{
      {"applyPatternImpl", "(JZLjava/lang/String;)V"},
      {"cloneImpl", "(J)J"},
      {"close", "(J)V"},
      {"formatLong",
       "(JJLlibcore/icu/NativeDecimalFormat$FieldPositionIterator;)[C"},
      {"getAttribute", "(JI)I"},
      {"getTextAttribute", "(JI)Ljava/lang/String;"},
      {"open",
       "(Ljava/lang/String;Ljava/lang/String;CCLjava/lang/String;CLjava/lang/"
       "String;Ljava/lang/String;CCLjava/lang/String;CCCC)J"},
      {"parse",
       "(JLjava/lang/String;Ljava/text/ParsePosition;Z)Ljava/lang/Number;"},
      {"setAttribute", "(JII)V"},
      {"setDecimalFormatSymbols",
       "(JLjava/lang/String;CCLjava/lang/String;CLjava/lang/String;Ljava/lang/"
       "String;CCLjava/lang/String;CCCC)V"},
      {"setRoundingMode", "(JID)V"},
      {"setSymbol", "(JILjava/lang/String;)V"},
      {"setTextAttribute", "(JILjava/lang/String;)V"},
      {"toPatternImpl", "(JZ)Ljava/lang/String;"}};
  for (const auto &[name, descriptor] : methods)
    b.GuestNativeStatic(name, descriptor,
                        kAccPrivate | kAccStatic | kAccNative);
  b.UnimplementedStatic("formatDigitList",
                        "(JLjava/lang/String;Llibcore/icu/"
                        "NativeDecimalFormat$FieldPositionIterator;)[C",
                        kAccPrivate | kAccStatic | kAccNative);
  b.UnimplementedStatic(
      "formatDouble",
      "(JDLlibcore/icu/NativeDecimalFormat$FieldPositionIterator;)[C",
      kAccPrivate | kAccStatic | kAccNative);
  return std::move(b).Build();
}

IntrinsicClassDecl TimeZoneNamesBoundary() {
  auto b = IntrinsicClassBuilder::Class("Llibcore/icu/TimeZoneNames;");
  b.GuestNativeStatic("fillZoneStrings",
                      "(Ljava/lang/String;[[Ljava/lang/String;)V",
                      kAccPrivate | kAccStatic | kAccNative);
  return std::move(b).Build();
}

IntrinsicClassDecl TimeZoneBoundary(const CoreIntrinsicServices &services) {
  auto b = IntrinsicClassBuilder::Class("Ljava/util/TimeZone;");
  const auto gmt = b.BoundStaticField("GMT", "Ljava/util/TimeZone;",
                                      kAccPrivate | kAccStatic | kAccFinal);
  const auto utc = b.BoundStaticField("UTC", "Ljava/util/TimeZone;",
                                      kAccPrivate | kAccStatic | kAccFinal);
  const auto default_zone = b.BoundStaticField(
      "defaultTimeZone", "Ljava/util/TimeZone;", kAccPrivate | kAccStatic);
  const auto ids = [](IntrinsicContext &c) {
    return VmValue::Ref(StringArray(c.vm, {u"GMT", u"UTC"}));
  };
  b.StaticMethod("getAvailableIDs", "()[Ljava/lang/String;", ids,
                 kAccPublic | kAccSynchronized);
  b.StaticMethod(
      "getAvailableIDs", "(I)[Ljava/lang/String;",
      [ids](IntrinsicContext &c) {
        return c.arguments[0].AsInt() == 0
                   ? ids(c)
                   : VmValue::Ref(StringArray(c.vm, {}));
      },
      kAccPublic | kAccSynchronized);
  b.StaticMethod(
      "getDefault", "()Ljava/util/TimeZone;",
      [default_zone, initial = services.default_timezone](IntrinsicContext &c) {
        IntrinsicCall call(c);
        auto zone = call.GetRef(default_zone);
        if (!zone.IsValid()) {
          const auto owner =
              call.Vm().Linker().ResolveDescriptor("Ljava/util/TimeZone;");
          const auto method = call.Vm().Linker().FindDirectMethod(
              owner, "getTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;");
          if (!method)
            throw DexVmError{DexVmErrorReason::invalid_member,
                             "TimeZone.getTimeZone is missing"};
          const auto outcome = call.Vm().Call(
              *method,
              std::array{VmValue::Ref(call.Vm().NewStringUtf8(initial))});
          if (outcome.exception.IsValid()) {
            call.Vm().SetPendingException(outcome.exception);
            return VmValue::Ref(VmObjectRef{});
          }
          zone = outcome.value.ref;
          call.SetRef(default_zone, zone);
        }
        return VmValue::Ref(call.Vm().CloneObject(zone));
      },
      kAccPublic | kAccSynchronized);
  b.StaticMethod(
      "getTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;",
      [gmt, utc](IntrinsicContext &c) {
        IntrinsicCall call(c);
        const auto ref = call.NonNullRef(0, "id");
        const auto id = call.Vm().StringUtf8(ref);
        if (id == "GMT")
          return VmValue::Ref(call.Vm().CloneObject(call.GetRef(gmt)));
        if (id == "UTC")
          return VmValue::Ref(call.Vm().CloneObject(call.GetRef(utc)));
        if (id.starts_with("GMT+") || id.starts_with("GMT-")) {
          const auto owner =
              call.Vm().Linker().ResolveDescriptor("Ljava/util/TimeZone;");
          const auto method = call.Vm().Linker().FindDirectMethod(
              owner, "getCustomTimeZone",
              "(Ljava/lang/String;)Ljava/util/TimeZone;");
          if (!method)
            throw DexVmError{DexVmErrorReason::invalid_member,
                             "TimeZone.getCustomTimeZone is missing"};
          const auto outcome =
              call.Vm().Call(*method, std::array{VmValue::Ref(ref)});
          if (outcome.exception.IsValid()) {
            call.Vm().SetPendingException(outcome.exception);
            return VmValue::Ref(VmObjectRef{});
          }
          return outcome.value.ref.IsValid()
                     ? outcome.value
                     : VmValue::Ref(call.Vm().CloneObject(call.GetRef(gmt)));
        }
        throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                          "named time-zone database is not packaged: " + id};
      },
      kAccPublic | kAccSynchronized);
  return std::move(b).Build();
}

} // namespace

void AppendJavaIcu(std::vector<IntrinsicClassDecl> &catalog,
                   const CoreIntrinsicServices &services) {
  catalog.push_back(IcuBoundary());
  catalog.push_back(DecimalBoundary());
  catalog.push_back(TimeZoneNamesBoundary());
  catalog.push_back(TimeZoneBoundary(services));
}

} // namespace ogplay::runtime::dexvm::intrinsics
