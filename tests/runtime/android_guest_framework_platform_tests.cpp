#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/integration/jni_guest_static_calls.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace {

constexpr std::uint64_t kThread = 81U;

[[nodiscard]] ogplay::runtime::JniReference MakeString(
    ogplay::runtime::JniEnvironment& environment,
    ogplay::runtime::JniStringStore& strings,
    const std::string_view value) {
    const std::vector<std::uint8_t> encoded(value.begin(), value.end());
    return environment.PublishLocalObject(
        kThread, strings.CreateModifiedUtf8(encoded));
}

[[nodiscard]] std::string ReadString(
    ogplay::runtime::JniEnvironment& environment,
    ogplay::runtime::JniStringStore& strings,
    const ogplay::runtime::JniReference reference) {
    const auto identity = environment.ResolveObjectForHle(kThread, reference);
    REQUIRE(identity.has_value());
    const auto encoded = strings.ModifiedUtf8Region(
        *identity, 0, strings.Length(*identity));
    return {encoded.begin(), encoded.end()};
}

}  // namespace

TEST_CASE("legacy Android platform identity installs one complete class group") {
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
         .host_name = "fixture-model"});
    const auto platform = InstallAndroidGuestFrameworkPlatform(
        classes, invocations, environment, strings, fields, objects, kThread,
        {.installation_id = "fixture-device",
         .host_name = "fixture-model"});

    constexpr std::string_view class_names[]{
        "java/lang/Object",
        "android/content/Context",
        "android/content/ContentResolver",
        "android/app/Activity",
        "android/telephony/TelephonyManager",
        "android/os/Build",
        "android/os/Build$VERSION",
        "android/os/SystemProperties",
        "android/provider/Settings$Secure",
        "android/os/Bundle",
        "android/view/ViewRoot",
        "java/util/UUID",
    };
    for (const auto name : class_names) {
        CHECK(classes.FindClass(std::string{name}).has_value());
    }

    const auto build = *classes.FindClass("android/os/Build");
    const auto version = *classes.FindClass("android/os/Build$VERSION");
    constexpr std::string_view build_string_fields[]{
        "BOARD", "BRAND", "CPU_ABI", "DEVICE", "MANUFACTURER",
        "MODEL", "PRODUCT", "SERIAL", "TAGS"};
    for (const auto name : build_string_fields) {
        CHECK(classes.GetFieldId(
            build, std::string{name}, "Ljava/lang/String;", true).has_value());
    }
    CHECK(classes.GetFieldId(
        version, "RELEASE", "Ljava/lang/String;", true).has_value());
    CHECK(classes.GetFieldId(
        version, "SDK", "Ljava/lang/String;", true).has_value());
    const auto context_service = *classes.GetFieldId(
        platform.context_class, "TELEPHONY_SERVICE", "Ljava/lang/String;",
        true);
    const auto serial = *classes.GetFieldId(
        build, "SERIAL", "Ljava/lang/String;", true);
    const auto sdk = *classes.GetFieldId(version, "SDK_INT", "I", true);
    CHECK(ReadString(environment, strings,
                     std::get<JniReference>(fields.GetStatic(
                         platform.context_class, context_service))) ==
          "phone");
    CHECK(ReadString(environment, strings,
                     std::get<JniReference>(
                         fields.GetStatic(build, serial))) ==
          "fixture-device");
    CHECK(std::get<JniInt>(fields.GetStatic(version, sdk)) == 19);

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
    const auto invoke_virtual = [&](const JniReference receiver,
                                    const JniObjectIdentity java_class,
                                    const char* name, const char* descriptor,
                                    const std::span<const JniValue> arguments = {}) {
        const auto method = classes.GetMethodId(
            java_class, name, descriptor, false);
        REQUIRE(method.has_value());
        return invocations.InvokeVirtual(
            kThread, receiver, java_class, *method, arguments,
            JniArgumentSource::value_array);
    };

    const auto context = environment.PublishLocalObject(
        kThread, platform.context);
    const std::array<JniValue, 1> phone{MakeString(
        environment, strings, "phone")};
    const auto telephony = std::get<JniReference>(invoke_virtual(
        context, platform.context_class, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;", phone));
    REQUIRE(environment.ResolveObjectForHle(kThread, telephony) ==
            platform.telephony);
    CHECK(ReadString(
              environment, strings,
              std::get<JniReference>(invoke_virtual(
                  telephony, platform.telephony_class, "getDeviceId",
                  "()Ljava/lang/String;"))) == "fixture-device");

    const auto resolver = std::get<JniReference>(invoke_virtual(
        context, platform.context_class, "getContentResolver",
        "()Landroid/content/ContentResolver;"));
    const auto secure = *classes.FindClass(
        "android/provider/Settings$Secure");
    const std::array<JniValue, 2> secure_arguments{
        resolver, MakeString(environment, strings, "android_id")};
    CHECK(ReadString(
              environment, strings,
              std::get<JniReference>(invoke_static(
                  secure, "getString",
                  "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;",
                  secure_arguments))) == "fixture-device");

    const auto properties = *classes.FindClass(
        "android/os/SystemProperties");
    const std::array<JniValue, 1> property{
        MakeString(environment, strings, "ro.serialno")};
    CHECK(ReadString(
              environment, strings,
              std::get<JniReference>(invoke_static(
                  properties, "get",
                  "(Ljava/lang/String;)Ljava/lang/String;", property))) ==
          "fixture-device");

    const auto uuid = std::get<JniReference>(invoke_static(
        platform.uuid_class, "randomUUID", "()Ljava/util/UUID;"));
    REQUIRE(environment.ResolveObjectForHle(kThread, uuid) == platform.uuid);
    CHECK(ReadString(
              environment, strings,
              std::get<JniReference>(invoke_virtual(
                  uuid, platform.uuid_class, "toString",
                  "()Ljava/lang/String;"))) ==
          "00000000-0000-4000-8000-000000000001");

    const auto game = classes.RegisterClass(
        {"fixture/Game", {},
         {{"d", "()V", "analytics.track_launch", true},
          {"da", "()Landroid/app/Activity;", "activity.current", true},
          {"db", "()[B", "device.identifier_bytes", true},
          {"dc", "()[B", "device.identifier_bytes", true}}, {}});
    static_cast<void>(invoke_static(game, "d", "()V"));
    CHECK(state.OfflineTrackingCount() == 1U);
    const auto activity = std::get<JniReference>(invoke_static(
        game, "da", "()Landroid/app/Activity;"));
    CHECK(environment.ResolveObjectForHle(kThread, activity) ==
          platform.context);
    for (const auto method : {"db", "dc"}) {
        const auto bytes = std::get<JniReference>(invoke_static(
            game, method, "()[B"));
        const auto identity = environment.ResolveObjectForHle(kThread, bytes);
        REQUIRE(identity.has_value());
        CHECK(arrays.Length(*identity) == 14);
    }
}
