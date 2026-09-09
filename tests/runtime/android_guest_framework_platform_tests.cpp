#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace {

constexpr std::uint64_t kThread = 81U;

}  // namespace

TEST_CASE("legacy platform leaves VM-owned framework classes unregistered") {
    using namespace ogplay::runtime;
    JniClassRegistry classes;
    JniInvocationEngine invocations{classes};
    JniEnvironment environment;
    environment.AttachThread(kThread, 64U);
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JniFieldStore fields{classes};
    JniGuestObjectRegistry objects{classes};
    AndroidGuestPlatformState state;
    BindAndroidGuestJavaPlatformHandlers(
        invocations, environment, strings, arrays, state,
        {.installation_id = "fixture-device",
         .android_id = "0123456789abcdef",
         .host_name = "fixture-model"});
    InstallAndroidGuestFrameworkPlatform(classes);
    CHECK(classes.FindClass("java/lang/Object").has_value());
    CHECK(classes.FindClass("android/view/ViewRoot").has_value());
    for (const auto name : {"android/os/Build", "android/os/Build$VERSION",
                            "android/os/SystemProperties", "android/os/Bundle",
                            "android/content/Context", "android/app/Activity",
                            "android/content/ContentResolver",
                            "android/telephony/TelephonyManager",
                            "android/provider/Settings$Secure"}) {
        CHECK_FALSE(classes.FindClass(name).has_value());
    }

    const auto invoke_static = [&](const JniObjectIdentity java_class,
                                   const char* name, const char* descriptor,
                                   const std::span<const JniValue> arguments = {}) {
        const auto method = classes.GetMethodId(
            java_class, name, descriptor, true);
        REQUIRE(method.has_value());
        return invocations.InvokeStatic(
            kThread, java_class, *method, arguments,
            JniArgumentSource::value_array);
    };
    CHECK_FALSE(classes.FindClass("java/util/UUID").has_value());

    const auto game = classes.RegisterClass(
        {"fixture/Game", {},
         {{"d", "()V", "analytics.track_launch", true},
          {"TrackingRegisterFirstRun", "()V", "analytics.track_first_run",
           true},
          {"increaseNumOfLaunch", "()V", "analytics.increase_launch_count",
           true},
          {"GetNumbOfLaunch", "()I", "analytics.get_launch_count", true},
          {"db", "()[B", "device.identifier_bytes", true},
          {"dc", "()[B", "device.identifier_bytes", true}}, {}});
    static_cast<void>(invoke_static(game, "d", "()V"));
    CHECK(state.OfflineTrackingCount() == 1U);
    static_cast<void>(invoke_static(game, "TrackingRegisterFirstRun", "()V"));
    CHECK(state.OfflineTrackingCount() == 2U);
    CHECK(std::get<JniInt>(invoke_static(game, "GetNumbOfLaunch", "()I")) ==
          JniInt{0});
    static_cast<void>(invoke_static(game, "increaseNumOfLaunch", "()V"));
    static_cast<void>(invoke_static(game, "increaseNumOfLaunch", "()V"));
    CHECK(std::get<JniInt>(invoke_static(game, "GetNumbOfLaunch", "()I")) ==
          JniInt{2});
    for (const auto method : {"db", "dc"}) {
        const auto bytes = std::get<JniReference>(invoke_static(
            game, method, "()[B"));
        const auto identity = environment.ResolveObjectForHle(kThread, bytes);
        REQUIRE(identity.has_value());
        CHECK(arrays.Length(*identity) == 14);
    }
}
