#include <array>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/framework_lifecycle.h"

namespace {

[[nodiscard]] ogplay::runtime::JniMethodId Method(
    const ogplay::runtime::JniClassRegistry& classes,
    const ogplay::runtime::JniObjectIdentity type, const char* name,
    const char* descriptor) {
    return *classes.GetMethodId(type, name, descriptor, false);
}

}  // namespace

TEST_CASE("framework HLE installs a declaration-only Activity class set") {
    ogplay::runtime::JniClassRegistry classes;
    ogplay::runtime::JniInvocationEngine invocations(classes);
    ogplay::runtime::FrameworkLifecycleHle lifecycle(classes, invocations);
    const auto installed = lifecycle.Install();
    CHECK(classes.FindClass("java/lang/Object") == installed.object_class);
    CHECK(classes.FindClass("android/content/Context") ==
          installed.context_class);
    CHECK(classes.FindClass("android/content/ContextWrapper") ==
          installed.context_wrapper_class);
    CHECK(classes.FindClass("android/os/Bundle") == installed.bundle_class);
    CHECK(classes.FindClass("android/app/Activity") ==
          installed.activity_class);
    CHECK(classes.GetSuperclass(installed.activity_class) ==
          installed.context_wrapper_class);
    CHECK(classes.GetSuperclass(installed.context_wrapper_class) ==
          installed.context_class);
    CHECK(classes.GetSuperclass(installed.context_class) ==
          installed.object_class);
    const auto get_assets = classes.GetMethodId(
        installed.activity_class, "getAssets",
        "()Landroid/content/res/AssetManager;", false);
    REQUIRE(get_assets.has_value());
    CHECK_THROWS_WITH_AS(
        static_cast<void>(invocations.InvokeVirtual(
            1, ogplay::runtime::JniReference{1}, installed.activity_class,
            *get_assets, {}, ogplay::runtime::JniArgumentSource::value_array)),
        "JNI method implementation has no registered handler",
        ogplay::runtime::JniInvocationError);
    CHECK_THROWS_AS(static_cast<void>(lifecycle.Install()),
                    ogplay::runtime::FrameworkLifecycleError);
}

TEST_CASE("framework Activity lifecycle records strict deterministic events") {
    ogplay::runtime::JniClassRegistry classes;
    ogplay::runtime::JniInvocationEngine invocations(classes);
    ogplay::runtime::FrameworkLifecycleHle lifecycle(classes, invocations);
    const auto installed = lifecycle.Install();
    const ogplay::runtime::JniReference activity{100};
    const std::array callbacks{
        std::pair{"<init>", "()V"},
        std::pair{"onCreate", "(Landroid/os/Bundle;)V"},
        std::pair{"onStart", "()V"}, std::pair{"onResume", "()V"},
        std::pair{"onPause", "()V"}, std::pair{"onStop", "()V"},
        std::pair{"onDestroy", "()V"},
    };
    for (const auto& [name, descriptor] : callbacks) {
        std::vector<ogplay::runtime::JniValue> arguments;
        if (std::string_view{name} == "onCreate") {
            arguments.emplace_back(ogplay::runtime::JniReference{});
        }
        static_cast<void>(invocations.InvokeNonvirtual(
            7, activity, installed.activity_class, installed.activity_class,
            Method(classes, installed.activity_class, name, descriptor),
            arguments, ogplay::runtime::JniArgumentSource::value_array));
    }
    CHECK(lifecycle.State(activity) ==
          ogplay::runtime::FrameworkActivityState::destroyed);
    const auto events = lifecycle.Events();
    REQUIRE(events.size() == callbacks.size());
    CHECK(events.front().sequence == 1);
    CHECK(events.back().sequence == callbacks.size());
    CHECK(events.back().callback == "onDestroy");
}

TEST_CASE("framework Activity lifecycle rejects skipped and duplicate steps") {
    ogplay::runtime::JniClassRegistry classes;
    ogplay::runtime::JniInvocationEngine invocations(classes);
    ogplay::runtime::FrameworkLifecycleHle lifecycle(classes, invocations);
    const auto installed = lifecycle.Install();
    const ogplay::runtime::JniReference activity{200};
    const auto construct =
        Method(classes, installed.activity_class, "<init>", "()V");
    const auto resume =
        Method(classes, installed.activity_class, "onResume", "()V");
    static_cast<void>(invocations.InvokeNonvirtual(
        8, activity, installed.activity_class, installed.activity_class,
        construct, {}, ogplay::runtime::JniArgumentSource::value_array));
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeVirtual(
            8, activity, installed.activity_class, resume, {},
            ogplay::runtime::JniArgumentSource::value_array)),
        ogplay::runtime::FrameworkLifecycleError);
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeNonvirtual(
            8, activity, installed.activity_class, installed.activity_class,
            construct, {}, ogplay::runtime::JniArgumentSource::value_array)),
        ogplay::runtime::FrameworkLifecycleError);
}
