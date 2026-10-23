// API 19 java.util locale and timer host boundaries.
#include "catalog.h"
#include "shared.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

namespace {

IntrinsicClassDecl DeclarePlatformLocale(
    const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/util/Locale;", "Ljava/lang/Object;",
        {"Ljava/lang/Cloneable;", "Ljava/io/Serializable;"},
        kAccPublic | kAccFinal);
    struct Fields final {
        IntrinsicFieldHandle language;
        IntrinsicFieldHandle country;
        IntrinsicFieldHandle variant;
        IntrinsicFieldHandle script;
    };
    const Fields fields{
        builder.BoundInstanceField("languageCode", "Ljava/lang/String;",
                                   kAccPrivate | kAccTransient),
        builder.BoundInstanceField("countryCode", "Ljava/lang/String;",
                                   kAccPrivate | kAccTransient),
        builder.BoundInstanceField("variantCode", "Ljava/lang/String;",
                                   kAccPrivate | kAccTransient),
        builder.BoundInstanceField("scriptCode", "Ljava/lang/String;",
                                   kAccPrivate | kAccTransient)};
    struct Constant final {
        const char* name;
        const char* language;
        const char* country;
    };
    static constexpr Constant constants[]{
        {"CANADA", "en", "CA"}, {"CANADA_FRENCH", "fr", "CA"},
        {"CHINA", "zh", "CN"}, {"CHINESE", "zh", ""},
        {"ENGLISH", "en", ""}, {"FRANCE", "fr", "FR"},
        {"FRENCH", "fr", ""}, {"GERMAN", "de", ""},
        {"GERMANY", "de", "DE"}, {"ITALIAN", "it", ""},
        {"ITALY", "it", "IT"}, {"JAPAN", "ja", "JP"},
        {"JAPANESE", "ja", ""}, {"KOREA", "ko", "KR"},
        {"KOREAN", "ko", ""}, {"PRC", "zh", "CN"},
        {"ROOT", "", ""}, {"SIMPLIFIED_CHINESE", "zh", "CN"},
        {"TAIWAN", "zh", "TW"}, {"TRADITIONAL_CHINESE", "zh", "TW"},
        {"UK", "en", "GB"}, {"US", "en", "US"}};
    std::vector<IntrinsicFieldHandle> constant_fields;
    constant_fields.reserve(std::size(constants));
    for (const auto& constant : constants) {
        constant_fields.push_back(builder.BoundStaticField(
            constant.name, "Ljava/util/Locale;", kAccPublic | kAccFinal));
    }
    const auto initialize = [fields](IntrinsicContext& context,
                                     const VmObjectRef object,
                                     const std::string_view language,
                                     const std::string_view country,
                                     const std::string_view variant = {}) {
        IntrinsicCall call(context);
        std::string normalized_language(language), normalized_country(country);
        for (auto& c : normalized_language) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        for (auto& c : normalized_country) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (normalized_language == "he") normalized_language = "iw";
        if (normalized_language == "id") normalized_language = "in";
        if (normalized_language == "yi") normalized_language = "ji";
        call.SetRef(fields.language, object,
                    call.Vm().NewStringUtf8(normalized_language));
        call.SetRef(fields.country, object,
                    call.Vm().NewStringUtf8(normalized_country));
        call.SetRef(fields.variant, object,
                    call.Vm().NewStringUtf8(variant));
        call.SetRef(fields.script, object, call.Vm().NewStringUtf8(""));
    };
    builder.ClassInitializer(
        [fields, constant_fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            for (std::size_t index = 0; index < std::size(constants); ++index) {
                const auto locale = call.Vm().NewIntrinsicInstance(
                    "Ljava/util/Locale;");
                const std::array roots{locale};
                const auto root_scope = call.Vm().ProtectReferences(roots);
                call.SetRef(fields.language, locale,
                            call.Vm().NewStringUtf8(constants[index].language));
                call.SetRef(fields.country, locale,
                            call.Vm().NewStringUtf8(constants[index].country));
                call.SetRef(fields.variant, locale,
                            call.Vm().NewStringUtf8(""));
                call.SetRef(fields.script, locale,
                            call.Vm().NewStringUtf8(""));
                call.SetRef(constant_fields[index], locale);
            }
            return VmValue::Void();
        });
    builder.Constructor("(Ljava/lang/String;)V",
        [initialize](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto language = call.NonNullRef(0, "language");
            initialize(context, call.Receiver(), call.Vm().StringUtf8(language), "");
            return VmValue::Void();
        });
    builder.Constructor("(Ljava/lang/String;Ljava/lang/String;)V",
        [initialize](IntrinsicContext& context) {
            IntrinsicCall call(context);
            initialize(context, call.Receiver(),
                       call.Vm().StringUtf8(call.NonNullRef(0, "language")),
                       call.Vm().StringUtf8(call.NonNullRef(1, "country")));
            return VmValue::Void();
        });
    builder.Constructor(
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
        [initialize](IntrinsicContext& context) {
            IntrinsicCall call(context);
            initialize(context, call.Receiver(),
                       call.Vm().StringUtf8(call.NonNullRef(0, "language")),
                       call.Vm().StringUtf8(call.NonNullRef(1, "country")),
                       call.Vm().StringUtf8(call.NonNullRef(2, "variant")));
            return VmValue::Void();
        });
    builder.StaticMethod(
        "getDefault", "()Ljava/util/Locale;",
        [services, fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto locale = services.singleton
                                    ? services.singleton(
                                          call.Vm(), "locale",
                                          "Ljava/util/Locale;")
                                    : call.Vm().NewIntrinsicInstance(
                                          "Ljava/util/Locale;");
            const std::array roots{locale};
            const auto root_scope = call.Vm().ProtectReferences(roots);
            if (!call.GetRef(fields.language, locale).IsValid()) {
                auto country = std::string{};
                if (services.language == "en" && services.iso3_country == "USA") country = "US";
                if (services.language == "zh" && services.iso3_country == "CHN") country = "CN";
                call.SetRef(fields.language, locale,
                            call.Vm().NewStringUtf8(services.language));
                call.SetRef(fields.country, locale,
                            call.Vm().NewStringUtf8(country));
                call.SetRef(fields.variant, locale, call.Vm().NewStringUtf8(""));
                call.SetRef(fields.script, locale, call.Vm().NewStringUtf8(""));
            }
            return VmValue::Ref(locale);
        });
    builder.StaticMethod("setDefault", "(Ljava/util/Locale;)V",
        [](IntrinsicContext&) -> VmValue {
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "Locale.setDefault is session configured"};
        }, kAccPublic | kAccStatic | kAccSynchronized);
    for (const auto* name : {"getISOLanguages", "getISOCountries"}) {
        builder.StaticMethod(name, "()[Ljava/lang/String;",
            [name](IntrinsicContext& context) {
                auto& vm = context.vm;
                // API 19 Locale delegates to ICU's Java cache and defensive clone.
                const auto method = vm.Linker().FindDirectMethod(
                    vm.Linker().ResolveDescriptor("Llibcore/icu/ICU;"),
                    name, "()[Ljava/lang/String;");
                if (!method) throw DexVmError(
                    DexVmErrorReason::unresolved_reference,
                    std::string("BootDex ICU method unavailable: ") + name);
                const auto result = vm.Call(*method, {});
                if (result.exception.IsValid()) throw VmJavaThrow{
                    vm.Linker().Class(result.exception_class).descriptor,
                    result.exception_message, result.exception};
                return result.value;
            });
    }
    builder.FinalMethod(
        "getLanguage", "()Ljava/lang/String;",
        [fields](IntrinsicContext& context) {
            return VmValue::Ref(IntrinsicCall(context).GetRef(fields.language));
        });
    builder.FinalMethod("getCountry", "()Ljava/lang/String;",
        [fields](IntrinsicContext& context) {
            return VmValue::Ref(IntrinsicCall(context).GetRef(fields.country));
        });
    builder.FinalMethod("getVariant", "()Ljava/lang/String;",
        [fields](IntrinsicContext& context) {
            return VmValue::Ref(IntrinsicCall(context).GetRef(fields.variant));
        });
    builder.FinalMethod("getScript", "()Ljava/lang/String;",
        [fields](IntrinsicContext& context) {
            return VmValue::Ref(IntrinsicCall(context).GetRef(fields.script));
        });
    builder.FinalMethod(
        "getISO3Language", "()Ljava/lang/String;",
        [language = services.iso3_language](IntrinsicContext& call) {
            return VmValue::Ref(call.vm.NewStringUtf8(language));
        });
    builder.FinalMethod(
        "getISO3Country", "()Ljava/lang/String;",
        [country = services.iso3_country](IntrinsicContext& call) {
            return VmValue::Ref(call.vm.NewStringUtf8(country));
        });
    builder.FinalOverrideMethod("toString", "()Ljava/lang/String;",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            auto text = call.Vm().StringUtf8(call.GetRef(fields.language));
            const auto country = call.Vm().StringUtf8(call.GetRef(fields.country));
            const auto variant = call.Vm().StringUtf8(call.GetRef(fields.variant));
            if (!country.empty() || !variant.empty()) text += "_" + country;
            if (!variant.empty()) text += "_" + variant;
            return VmValue::Ref(call.Vm().NewStringUtf8(text));
        });
    builder.FinalOverrideMethod("hashCode", "()I",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto text = call.Vm().StringUtf8(call.GetRef(fields.language)) +
                              "_" + call.Vm().StringUtf8(call.GetRef(fields.country)) +
                              "_" + call.Vm().StringUtf8(call.GetRef(fields.variant));
            return VmValue::Int(detail::JavaUtf8Hash(context, text));
        });
    builder.FinalOverrideMethod("equals", "(Ljava/lang/Object;)Z",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto other = call.Ref(0);
            if (!other.IsValid() || call.Vm().Linker().Class(
                    call.Vm().Model().ObjectClass(other)).descriptor !=
                    "Ljava/util/Locale;") return VmValue::Int(0);
            const auto same = [&](const IntrinsicFieldHandle field) {
                return call.Vm().StringUtf8(call.GetRef(field)) ==
                       call.Vm().StringUtf8(call.GetRef(field, other));
            };
            return VmValue::Int(same(fields.language) && same(fields.country) &&
                                same(fields.variant) && same(fields.script));
        });
    builder.OverrideMethod("clone", "()Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.Model().CloneObject(context.receiver));
        });
    return std::move(builder).Build();
}



}  // namespace

void AppendJavaUtilPlatform(std::vector<IntrinsicClassDecl>& catalog,
                            const CoreIntrinsicServices& services) {
    catalog.push_back(DeclarePlatformLocale(services));
}

}  // namespace ogplay::runtime::dexvm::intrinsics
