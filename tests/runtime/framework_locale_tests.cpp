#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/framework_lifecycle.h"
#include "ogplay/runtime/framework_locale.h"

namespace {

using namespace ogplay::runtime;

[[nodiscard]] JniMethodId Method(const JniClassRegistry& classes,
                                 const JniObjectIdentity type,
                                 const char* name, const char* descriptor,
                                 const bool is_static = false) {
    return *classes.GetMethodId(type, name, descriptor, is_static);
}

[[nodiscard]] std::vector<JniChar> Text(
    JniEnvironment& environment, JniStringStore& strings,
    const std::uint64_t thread, const JniValue& value) {
    const auto reference = std::get<JniReference>(value);
    const auto identity = *environment.ResolveObjectForHle(thread, reference);
    return strings.Region(identity, 0, strings.Length(identity));
}

}  // namespace

TEST_CASE("framework Locale exposes one deterministic injected default") {
    JniClassRegistry classes;
    JniInvocationEngine invocations(classes);
    JniEnvironment environment;
    JniStringStore strings;
    FrameworkLifecycleHle lifecycle(classes, invocations);
    static_cast<void>(lifecycle.Install());
    FrameworkLocaleHle locale_hle(classes, invocations, environment, strings,
                                  {"zh", "CN"});
    const auto locale_class = locale_hle.Install();
    CHECK_THROWS_AS(static_cast<void>(locale_hle.Install()),
                    FrameworkLocaleError);

    constexpr std::uint64_t thread = 61;
    environment.AttachThread(thread, 12);
    const auto locale = std::get<JniReference>(invocations.InvokeStatic(
        thread, locale_class,
        Method(classes, locale_class, "getDefault", "()Ljava/util/Locale;",
               true),
        {}, JniArgumentSource::value_array));
    CHECK(Text(environment, strings, thread, invocations.InvokeVirtual(
                   thread, locale, locale_class,
                   Method(classes, locale_class, "getLanguage",
                          "()Ljava/lang/String;"),
                   {}, JniArgumentSource::value_array)) ==
          std::vector<JniChar>{'z', 'h'});
    CHECK(Text(environment, strings, thread, invocations.InvokeVirtual(
                   thread, locale, locale_class,
                   Method(classes, locale_class, "getCountry",
                          "()Ljava/lang/String;"),
                   {}, JniArgumentSource::value_array)) ==
          std::vector<JniChar>{'C', 'N'});
    CHECK(Text(environment, strings, thread, invocations.InvokeVirtual(
                   thread, locale, locale_class,
                   Method(classes, locale_class, "toString",
                          "()Ljava/lang/String;"),
                   {}, JniArgumentSource::value_array)) ==
          std::vector<JniChar>{'z', 'h', '_', 'C', 'N'});
    environment.DetachThread(thread);
}

TEST_CASE("framework Locale rejects invalid config and reference scope") {
    JniClassRegistry invalid_classes;
    JniInvocationEngine invalid_invocations(invalid_classes);
    JniEnvironment invalid_environment;
    JniStringStore invalid_strings;
    FrameworkLifecycleHle invalid_lifecycle(invalid_classes,
                                            invalid_invocations);
    static_cast<void>(invalid_lifecycle.Install());
    FrameworkLocaleHle invalid(invalid_classes, invalid_invocations,
                               invalid_environment, invalid_strings,
                               {"EN", "us"});
    CHECK_THROWS_AS(static_cast<void>(invalid.Install()),
                    FrameworkLocaleError);

    JniClassRegistry classes;
    JniInvocationEngine invocations(classes);
    JniEnvironment environment;
    JniStringStore strings;
    FrameworkLifecycleHle lifecycle(classes, invocations);
    static_cast<void>(lifecycle.Install());
    FrameworkLocaleHle locale_hle(classes, invocations, environment, strings,
                                  {"ja", "JP"});
    const auto locale_class = locale_hle.Install();
    environment.AttachThread(62, 4);
    environment.AttachThread(63, 4);
    const auto locale = std::get<JniReference>(invocations.InvokeStatic(
        62, locale_class,
        Method(classes, locale_class, "getDefault", "()Ljava/util/Locale;",
               true),
        {}, JniArgumentSource::value_array));
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeVirtual(
            63, locale, locale_class,
            Method(classes, locale_class, "getLanguage",
                   "()Ljava/lang/String;"),
            {}, JniArgumentSource::value_array)),
        JniReferenceError);
    environment.DetachThread(63);
    environment.DetachThread(62);
}
