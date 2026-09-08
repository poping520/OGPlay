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

VmObjectRef StringArray(Interpreter& vm,
                        const std::vector<std::u16string>& values) {
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
  for (const auto& [name, descriptor, flags] : std::array{
           std::tuple{"getBestDateTimePatternNative", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", kAccPrivate | kAccStatic | kAccNative},
           std::tuple{"getCurrencyCode", "(Ljava/lang/String;)Ljava/lang/String;", kAccPublic | kAccStatic | kAccNative},
           std::tuple{"getCurrencyDisplayName", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", kAccPublic | kAccStatic | kAccNative},
           std::tuple{"getCurrencyFractionDigits", "(Ljava/lang/String;)I", kAccPublic | kAccStatic | kAccNative},
           std::tuple{"getCurrencySymbol", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", kAccPublic | kAccStatic | kAccNative},
           std::tuple{"getISOCountriesNative", "()[Ljava/lang/String;", kAccPrivate | kAccStatic | kAccNative},
           std::tuple{"getISOLanguagesNative", "()[Ljava/lang/String;", kAccPrivate | kAccStatic | kAccNative},
           std::tuple{"initLocaleDataNative", "(Ljava/lang/String;Llibcore/icu/LocaleData;)Z", kAccStatic | kAccNative},
           std::tuple{"toLowerCase", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", kAccPublic | kAccStatic | kAccNative},
           std::tuple{"toUpperCase", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", kAccPublic | kAccStatic | kAccNative}})
    b.GuestNativeStatic(name, descriptor, flags);
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
      {"getIcuVersion", "()Ljava/lang/String;"},
      {"getScript", "(Ljava/lang/String;)Ljava/lang/String;"},
      {"getUnicodeVersion", "()Ljava/lang/String;"},
      {"languageTagForLocale", "(Ljava/lang/String;)Ljava/lang/String;"},
      {"localeForLanguageTag", "(Ljava/lang/String;Z)Ljava/lang/String;"}};
  for (const auto& [name, descriptor] : failures)
    b.UnimplementedStatic(name, descriptor,
                          kAccPublic | kAccStatic | kAccNative);
  return std::move(b).Build();
}

IntrinsicClassDecl DecimalBoundary() {
  auto b = IntrinsicClassBuilder::Class("Llibcore/icu/NativeDecimalFormat;");
  constexpr std::pair<const char*, const char*> methods[]{
      {"applyPatternImpl", "(JZLjava/lang/String;)V"}, {"cloneImpl", "(J)J"},
      {"close", "(J)V"}, {"formatLong", "(JJLlibcore/icu/NativeDecimalFormat$FieldPositionIterator;)[C"},
      {"getAttribute", "(JI)I"}, {"getTextAttribute", "(JI)Ljava/lang/String;"},
      {"open", "(Ljava/lang/String;Ljava/lang/String;CCLjava/lang/String;CLjava/lang/String;Ljava/lang/String;CCLjava/lang/String;CCCC)J"},
      {"parse", "(JLjava/lang/String;Ljava/text/ParsePosition;Z)Ljava/lang/Number;"},
      {"setAttribute", "(JII)V"},
      {"setDecimalFormatSymbols", "(JLjava/lang/String;CCLjava/lang/String;CLjava/lang/String;Ljava/lang/String;CCLjava/lang/String;CCCC)V"},
      {"setRoundingMode", "(JID)V"}, {"setSymbol", "(JILjava/lang/String;)V"},
      {"setTextAttribute", "(JILjava/lang/String;)V"},
      {"toPatternImpl", "(JZ)Ljava/lang/String;"}};
  for (const auto& [name, descriptor] : methods)
    b.GuestNativeStatic(name, descriptor,
                        kAccPrivate | kAccStatic | kAccNative);
  b.UnimplementedStatic("formatDigitList", "(JLjava/lang/String;Llibcore/icu/NativeDecimalFormat$FieldPositionIterator;)[C", kAccPrivate | kAccStatic | kAccNative);
  b.UnimplementedStatic("formatDouble", "(JDLlibcore/icu/NativeDecimalFormat$FieldPositionIterator;)[C", kAccPrivate | kAccStatic | kAccNative);
  return std::move(b).Build();
}

IntrinsicClassDecl TimeZoneNamesBoundary() {
  auto b = IntrinsicClassBuilder::Class("Llibcore/icu/TimeZoneNames;");
  b.GuestNativeStatic("fillZoneStrings", "(Ljava/lang/String;[[Ljava/lang/String;)V",
                      kAccPrivate | kAccStatic | kAccNative);
  return std::move(b).Build();
}

IntrinsicClassDecl TimeZoneBoundary(const CoreIntrinsicServices& services) {
  auto b = IntrinsicClassBuilder::Class("Ljava/util/TimeZone;");
  const auto gmt = b.BoundStaticField("GMT", "Ljava/util/TimeZone;", kAccPrivate | kAccStatic | kAccFinal);
  const auto utc = b.BoundStaticField("UTC", "Ljava/util/TimeZone;", kAccPrivate | kAccStatic | kAccFinal);
  const auto default_zone = b.BoundStaticField("defaultTimeZone", "Ljava/util/TimeZone;", kAccPrivate | kAccStatic);
  const auto ids = [](IntrinsicContext& c) { return VmValue::Ref(StringArray(c.vm, {u"GMT", u"UTC"})); };
  b.StaticMethod("getAvailableIDs", "()[Ljava/lang/String;", ids, kAccPublic | kAccSynchronized);
  b.StaticMethod("getAvailableIDs", "(I)[Ljava/lang/String;", [ids](IntrinsicContext& c) {
    return c.arguments[0].AsInt() == 0 ? ids(c) : VmValue::Ref(StringArray(c.vm, {}));
  }, kAccPublic | kAccSynchronized);
  b.StaticMethod("getDefault", "()Ljava/util/TimeZone;",
    [default_zone, initial = services.default_timezone](IntrinsicContext& c) {
      IntrinsicCall call(c); auto zone = call.GetRef(default_zone);
      if (!zone.IsValid()) {
        const auto owner = call.Vm().Linker().ResolveDescriptor("Ljava/util/TimeZone;");
        const auto method = call.Vm().Linker().FindDirectMethod(owner, "getTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;");
        if (!method) throw DexVmError{DexVmErrorReason::invalid_member, "TimeZone.getTimeZone is missing"};
        const auto outcome = call.Vm().Call(*method, std::array{VmValue::Ref(call.Vm().NewStringUtf8(initial))});
        if (outcome.exception.IsValid()) { call.Vm().SetPendingException(outcome.exception); return VmValue::Ref(VmObjectRef{}); }
        zone = outcome.value.ref; call.SetRef(default_zone, zone);
      }
      return VmValue::Ref(call.Vm().CloneObject(zone));
    }, kAccPublic | kAccSynchronized);
  b.StaticMethod("getTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;", [gmt, utc](IntrinsicContext& c) {
    IntrinsicCall call(c); const auto ref = call.NonNullRef(0, "id"); const auto id = call.Vm().StringUtf8(ref);
    if (id == "GMT") return VmValue::Ref(call.Vm().CloneObject(call.GetRef(gmt)));
    if (id == "UTC") return VmValue::Ref(call.Vm().CloneObject(call.GetRef(utc)));
    if (id.starts_with("GMT+") || id.starts_with("GMT-")) {
      const auto owner = call.Vm().Linker().ResolveDescriptor("Ljava/util/TimeZone;");
      const auto method = call.Vm().Linker().FindDirectMethod(owner, "getCustomTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;");
      if (!method) throw DexVmError{DexVmErrorReason::invalid_member, "TimeZone.getCustomTimeZone is missing"};
      const auto outcome = call.Vm().Call(*method, std::array{VmValue::Ref(ref)});
      if (outcome.exception.IsValid()) { call.Vm().SetPendingException(outcome.exception); return VmValue::Ref(VmObjectRef{}); }
      return outcome.value.ref.IsValid() ? outcome.value : VmValue::Ref(call.Vm().CloneObject(call.GetRef(gmt)));
    }
    throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "named time-zone database is not packaged: " + id};
  }, kAccPublic | kAccSynchronized);
  return std::move(b).Build();
}

}  // namespace

void AppendJavaIcu(std::vector<IntrinsicClassDecl>& catalog,
                   const CoreIntrinsicServices& services) {
  catalog.push_back(IcuBoundary());
  catalog.push_back(DecimalBoundary());
  catalog.push_back(TimeZoneNamesBoundary());
  catalog.push_back(TimeZoneBoundary(services));
}

}  // namespace ogplay::runtime::dexvm::intrinsics
