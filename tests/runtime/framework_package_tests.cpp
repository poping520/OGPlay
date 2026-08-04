#include <array>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/framework_lifecycle.h"
#include "ogplay/runtime/framework_package.h"

namespace {

using namespace ogplay::runtime;

[[nodiscard]] JniMethodId Method(const JniClassRegistry& classes,
                                 const JniObjectIdentity type,
                                 const char* name, const char* descriptor) {
    return *classes.GetMethodId(type, name, descriptor, false);
}

[[nodiscard]] JniReference ReferenceResult(const JniValue& value) {
    return std::get<JniReference>(value);
}

}  // namespace

TEST_CASE("framework PackageManager returns current package version fields") {
    JniClassRegistry classes;
    JniInvocationEngine invocations(classes);
    JniEnvironment environment;
    JniStringStore strings;
    JniFieldStore fields(classes);
    FrameworkLifecycleHle lifecycle(classes, invocations);
    const auto framework = lifecycle.Install();
    FrameworkPackageHle packages(
        classes, invocations, environment, strings, fields,
        {"org.example.game", u"1.2.3", JniInt{42}});
    const auto package_classes = packages.Install();

    environment.AttachThread(71, 12);
    environment.AttachThread(72, 4);
    const auto activity = environment.PublishLocalObject(
        71, AllocateJniHostObjectIdentity());
    const auto package_name = ReferenceResult(invocations.InvokeVirtual(
        71, activity, framework.activity_class,
        Method(classes, framework.activity_class, "getPackageName",
               "()Ljava/lang/String;"),
        {}, JniArgumentSource::value_array));
    const auto name_identity =
        *environment.ResolveObjectForHle(71, package_name);
    CHECK(strings.Region(name_identity, 0, strings.Length(name_identity)) ==
          std::vector<JniChar>{'o', 'r', 'g', '.', 'e', 'x', 'a', 'm', 'p',
                               'l', 'e', '.', 'g', 'a', 'm', 'e'});
    const auto manager = ReferenceResult(invocations.InvokeVirtual(
        71, activity, framework.activity_class,
        Method(classes, framework.activity_class, "getPackageManager",
               "()Landroid/content/pm/PackageManager;"),
        {}, JniArgumentSource::value_array));
    const std::array<JniValue, 2> arguments{package_name, JniInt{0}};
    const auto info_reference = ReferenceResult(invocations.InvokeVirtual(
        71, manager, package_classes.package_manager_class,
        Method(classes, package_classes.package_manager_class,
               "getPackageInfo",
               "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;"),
        arguments, JniArgumentSource::value_array));
    const auto info = *environment.ResolveObjectForHle(71, info_reference);
    const auto version_code = *classes.GetFieldId(
        package_classes.package_info_class, "versionCode", "I", false);
    const auto version_name = *classes.GetFieldId(
        package_classes.package_info_class, "versionName",
        "Ljava/lang/String;", false);
    CHECK(std::get<JniInt>(fields.GetInstance(
              info, package_classes.package_info_class, version_code)) == 42);
    const auto version_reference = std::get<JniReference>(fields.GetInstance(
        info, package_classes.package_info_class, version_name));
    const auto version_identity =
        *environment.ResolveObjectForHle(72, version_reference);
    CHECK(strings.Region(version_identity, 0,
                         strings.Length(version_identity)) ==
          std::vector<JniChar>{'1', '.', '2', '.', '3'});

    environment.DetachThread(72);
    environment.DetachThread(71);
}

TEST_CASE("framework PackageManager rejects unsupported queries and config") {
    JniClassRegistry invalid_classes;
    JniInvocationEngine invalid_invocations(invalid_classes);
    JniEnvironment invalid_environment;
    JniStringStore invalid_strings;
    JniFieldStore invalid_fields(invalid_classes);
    FrameworkLifecycleHle invalid_lifecycle(invalid_classes,
                                            invalid_invocations);
    static_cast<void>(invalid_lifecycle.Install());
    FrameworkPackageHle invalid(
        invalid_classes, invalid_invocations, invalid_environment,
        invalid_strings, invalid_fields, {"bad..name", u"1", JniInt{1}});
    CHECK_THROWS_AS(static_cast<void>(invalid.Install()),
                    FrameworkPackageError);

    JniClassRegistry classes;
    JniInvocationEngine invocations(classes);
    JniEnvironment environment;
    JniStringStore strings;
    JniFieldStore fields(classes);
    FrameworkLifecycleHle lifecycle(classes, invocations);
    const auto framework = lifecycle.Install();
    FrameworkPackageHle packages(classes, invocations, environment, strings,
                                 fields,
                                 {"org.example", u"2", JniInt{2}});
    const auto package_classes = packages.Install();
    CHECK_THROWS_AS(static_cast<void>(packages.Install()),
                    FrameworkPackageError);
    environment.AttachThread(73, 12);
    const auto activity = environment.PublishLocalObject(
        73, AllocateJniHostObjectIdentity());
    const auto manager = ReferenceResult(invocations.InvokeVirtual(
        73, activity, framework.activity_class,
        Method(classes, framework.activity_class, "getPackageManager",
               "()Landroid/content/pm/PackageManager;"),
        {}, JniArgumentSource::value_array));
    const std::vector<JniChar> other_text{'o', 't', 'h', 'e', 'r'};
    const auto other_identity = strings.Create(other_text);
    const auto other = environment.PublishLocalObject(73, other_identity);
    const auto get_info = Method(
        classes, package_classes.package_manager_class, "getPackageInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
    const std::array<JniValue, 2> unknown{other, JniInt{0}};
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeVirtual(
            73, manager, package_classes.package_manager_class, get_info,
            unknown, JniArgumentSource::value_array)),
        FrameworkPackageError);
    const std::array<JniValue, 2> flags{other, JniInt{1}};
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeVirtual(
            73, manager, package_classes.package_manager_class, get_info,
            flags, JniArgumentSource::value_array)),
        FrameworkPackageError);
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeVirtual(
            73, JniReference{999}, package_classes.package_manager_class,
            get_info, unknown, JniArgumentSource::value_array)),
        JniReferenceError);
    environment.DetachThread(73);
    strings.Delete(other_identity);
}
