#include "ogplay/runtime/integration/android_guest_call_session.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_object.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"

namespace ogplay::runtime {
namespace {

JniValue PublishBytes(JniEnvironment& environment,
                      JniPrimitiveArrayStore& arrays,
                      const JniInvocation& invocation,
                      const std::string_view text) {
    const auto identity = arrays.New(
        JniPrimitiveKind::byte, static_cast<JniSize>(text.size()));
    try {
        std::vector<JniByte> bytes;
        bytes.reserve(text.size());
        for (const auto byte : text) {
            bytes.push_back(static_cast<JniByte>(
                static_cast<unsigned char>(byte)));
        }
        arrays.SetRegion(
            identity, 0, JniPrimitiveArrayData{std::move(bytes)});
        return JniValue{environment.PublishLocalObject(
            invocation.thread_id, identity)};
    } catch (...) {
        arrays.Delete(identity);
        throw;
    }
}

JniValue PublishString(JniEnvironment& environment, JniStringStore& strings,
                       const JniInvocation& invocation,
                       const std::string_view text) {
    std::vector<std::uint8_t> encoded;
    encoded.reserve(text.size());
    for (const auto byte : text) {
        encoded.push_back(static_cast<std::uint8_t>(byte));
    }
    const auto identity = strings.CreateModifiedUtf8(encoded);
    try {
        return JniValue{environment.PublishLocalObject(
            invocation.thread_id, identity)};
    } catch (...) {
        strings.Delete(identity);
        throw;
    }
}

std::vector<JniChar> ResolveOptionalString(
    JniEnvironment& environment, JniStringStore& strings,
    const JniInvocation& invocation, const std::size_t index) {
    const auto reference = std::get<JniReference>(invocation.arguments[index]);
    const auto identity = environment.ResolveObjectForHle(
        invocation.thread_id, reference);
    if (!identity.has_value()) return {};
    return strings.Region(*identity, 0, strings.Length(*identity));
}

std::string ResolveAsciiString(JniEnvironment& environment,
                               JniStringStore& strings,
                               const JniInvocation& invocation,
                               const std::size_t index) {
    const auto text = ResolveOptionalString(
        environment, strings, invocation, index);
    std::string result;
    result.reserve(text.size());
    for (const auto code_unit : text) {
        if (code_unit > 0x7fU) {
            throw AndroidGuestCallSessionError(
                "Android framework identity key must be ASCII");
        }
        result.push_back(static_cast<char>(code_unit));
    }
    return result;
}

JniReference PublishGlobalString(JniEnvironment& environment,
                                 JniStringStore& strings,
                                 const std::uint64_t thread_id,
                                 const std::string_view text) {
    std::vector<std::uint8_t> encoded;
    encoded.reserve(text.size());
    for (const auto byte : text) {
        encoded.push_back(static_cast<std::uint8_t>(byte));
    }
    const auto identity = strings.CreateModifiedUtf8(encoded);
    try {
        return environment.PublishGlobalObjectForHle(thread_id, identity);
    } catch (...) {
        strings.Delete(identity);
        throw;
    }
}

}  // namespace

void AndroidGuestPlatformState::SetUniqueCode(const std::int32_t value) {
    std::scoped_lock lock(mutex_);
    unique_code_ = value;
}
void AndroidGuestPlatformState::RequestBackground() {
    std::scoped_lock lock(mutex_);
    background_requested_ = true;
}
void AndroidGuestPlatformState::SetFullyLoaded() {
    std::scoped_lock lock(mutex_);
    fully_loaded_ = true;
}
void AndroidGuestPlatformState::SetKeyboard(
    const bool visible, const std::span<const JniChar> text) {
    std::scoped_lock lock(mutex_);
    keyboard_visible_ = visible;
    keyboard_text_.assign(text.begin(), text.end());
}
void AndroidGuestPlatformState::RequestManagedSwap() {
    std::scoped_lock lock(mutex_);
    ++managed_swap_requests_;
}
void AndroidGuestPlatformState::RecordOfflineTracking() {
    std::scoped_lock lock(mutex_);
    ++offline_tracking_count_;
}
void AndroidGuestPlatformState::IncreaseLaunchCount() {
    std::scoped_lock lock(mutex_);
    ++launch_count_;
}
std::optional<std::int32_t> AndroidGuestPlatformState::UniqueCode() const {
    std::scoped_lock lock(mutex_);
    return unique_code_;
}
bool AndroidGuestPlatformState::BackgroundRequested() const {
    std::scoped_lock lock(mutex_);
    return background_requested_;
}
bool AndroidGuestPlatformState::FullyLoaded() const {
    std::scoped_lock lock(mutex_);
    return fully_loaded_;
}
bool AndroidGuestPlatformState::KeyboardVisible() const {
    std::scoped_lock lock(mutex_);
    return keyboard_visible_;
}
std::vector<JniChar> AndroidGuestPlatformState::KeyboardText() const {
    std::scoped_lock lock(mutex_);
    return keyboard_text_;
}
std::uint64_t AndroidGuestPlatformState::ManagedSwapRequests() const {
    std::scoped_lock lock(mutex_);
    return managed_swap_requests_;
}
std::uint64_t AndroidGuestPlatformState::OfflineTrackingCount() const {
    std::scoped_lock lock(mutex_);
    return offline_tracking_count_;
}
std::int32_t AndroidGuestPlatformState::LaunchCount() const {
    std::scoped_lock lock(mutex_);
    return launch_count_;
}

void BindAndroidGuestJavaPlatformHandlers(
    JniInvocationEngine& invocations, JniEnvironment& environment,
    JniStringStore& strings, JniPrimitiveArrayStore& arrays,
    AndroidGuestPlatformState& state,
    const AndroidGuestPlatformConfig& config) {
    const auto bytes = [&invocations, &environment, &arrays](
                           const char* implementation,
                           const std::string& value) {
        invocations.RegisterHandler(
            implementation,
            [&environment, &arrays, value](const JniInvocation& invocation) {
                return PublishBytes(environment, arrays, invocation, value);
            });
    };
    bytes("device.identifier_bytes", config.installation_id);
    bytes("network.operator_bytes", config.operator_name);
    bytes("telephony.line_number_bytes", config.line_number);
    bytes("application.version_bytes", config.version_name);
    bytes("device.host_name_bytes", config.host_name);
    bytes("network.user_agent_bytes", config.user_agent);

    invocations.RegisterHandler(
        "network.wifi_enabled",
        [](const JniInvocation&) { return JniValue{JniBoolean{0}}; });
    invocations.RegisterHandler(
        "device.set_unique_code",
        [&state](const JniInvocation& invocation) {
            state.SetUniqueCode(std::get<JniInt>(invocation.arguments[0]));
            return JniValue{std::monostate{}};
        });
    const auto integer_fact = [&invocations](const char* implementation,
                                             const JniInt value) {
        invocations.RegisterHandler(
            implementation,
            [value](const JniInvocation&) { return JniValue{value}; });
    };
    integer_fact("network.wifi_enabled_int", 0);
    integer_fact("network.internet_available", 0);
    integer_fact("audio.external_music_active", 0);
    integer_fact("platform.firmware_before_22", 0);
    integer_fact("network.wifi_address", 0);

    const auto string_fact = [&invocations, &environment, &strings](
                                 const char* implementation,
                                 const std::string& value) {
        invocations.RegisterHandler(
            implementation,
            [&environment, &strings, value](const JniInvocation& invocation) {
                return PublishString(environment, strings, invocation, value);
            });
    };
    string_fact("network.mac_address", config.mac_address);
    string_fact("device.identifier_string", config.installation_id);

    invocations.RegisterHandler(
        "activity.send_to_background",
        [&state](const JniInvocation&) {
            state.RequestBackground();
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "activity.set_fully_loaded",
        [&state](const JniInvocation&) {
            state.SetFullyLoaded();
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "keyboard.set",
        [&environment, &strings, &state](const JniInvocation& invocation) {
            const auto enabled = std::get<JniInt>(invocation.arguments[0]) != 0;
            const auto text = ResolveOptionalString(
                environment, strings, invocation, 1);
            state.SetKeyboard(enabled, text);
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "keyboard.visible",
        [&state](const JniInvocation&) {
            return JniValue{JniInt{state.KeyboardVisible() ? 1 : 0}};
        });
    invocations.RegisterHandler(
        "keyboard.text_bytes",
        [&environment, &arrays, &state](const JniInvocation& invocation) {
            const auto text = state.KeyboardText();
            std::string bytes;
            bytes.reserve(text.size());
            for (const auto code_unit : text) {
                bytes.push_back(code_unit <= 0x7fU
                                    ? static_cast<char>(code_unit)
                                    : '?');
            }
            return PublishBytes(environment, arrays, invocation, bytes);
        });
    invocations.RegisterHandler(
        "display.swap_managed_surface",
        [&state](const JniInvocation&) {
            state.RequestManagedSwap();
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "analytics.track_launch",
        [&state](const JniInvocation&) {
            state.RecordOfflineTracking();
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "analytics.track_first_run",
        [&state](const JniInvocation&) {
            state.RecordOfflineTracking();
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "analytics.increase_launch_count",
        [&state](const JniInvocation&) {
            state.IncreaseLaunchCount();
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "analytics.get_launch_count",
        [&state](const JniInvocation&) {
            return JniValue{JniInt{state.LaunchCount()}};
        });
    invocations.RegisterHandler(
        "platform.unavailable",
        [](const JniInvocation& invocation) -> JniValue {
            throw AndroidGuestCallSessionError(
                "Android guest platform callback is unavailable: " +
                invocation.method.declaration.name +
                invocation.method.declaration.descriptor);
        });
}

AndroidGuestFrameworkPlatformSet InstallAndroidGuestFrameworkPlatform(
    JniClassRegistry& classes, JniInvocationEngine& invocations,
    JniEnvironment& environment, JniStringStore& strings,
    JniFieldStore& fields, JniGuestObjectRegistry& objects,
    const std::uint64_t thread_id,
    const AndroidGuestPlatformConfig& config) {
    const auto object_class = classes.RegisterClass(
        {"java/lang/Object", {}, {}, {}});
    const auto content_resolver_class = classes.RegisterClass(
        {"android/content/ContentResolver", "java/lang/Object", {}, {}});
    const auto telephony_class = classes.RegisterClass(
        {"android/telephony/TelephonyManager", "java/lang/Object",
         {{"getDeviceId", "()Ljava/lang/String;",
           "framework.telephony.get_device_id", false}}, {}});
    const auto context_class = classes.RegisterClass(
        {"android/content/Context", "java/lang/Object",
         {{"getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
           "framework.context.get_system_service", false},
          {"getContentResolver", "()Landroid/content/ContentResolver;",
           "framework.context.get_content_resolver", false}},
         {{"TELEPHONY_SERVICE", "Ljava/lang/String;",
           "framework.context.telephony_service", true}}});
    const auto activity_class = classes.RegisterClass(
        {"android/app/Activity", "android/content/Context", {}, {}});
    const auto build_class = classes.RegisterClass(
        {"android/os/Build", "java/lang/Object", {},
         {{"BOARD", "Ljava/lang/String;", "framework.build.board", true},
          {"BRAND", "Ljava/lang/String;", "framework.build.brand", true},
          {"CPU_ABI", "Ljava/lang/String;", "framework.build.cpu_abi", true},
          {"DEVICE", "Ljava/lang/String;", "framework.build.device", true},
          {"MANUFACTURER", "Ljava/lang/String;",
           "framework.build.manufacturer", true},
          {"MODEL", "Ljava/lang/String;", "framework.build.model", true},
          {"PRODUCT", "Ljava/lang/String;", "framework.build.product", true},
          {"SERIAL", "Ljava/lang/String;", "framework.build.serial", true},
          {"TAGS", "Ljava/lang/String;", "framework.build.tags", true}}});
    const auto version_class = classes.RegisterClass(
        {"android/os/Build$VERSION", "java/lang/Object", {},
         {{"RELEASE", "Ljava/lang/String;", "framework.build.release", true},
          {"SDK", "Ljava/lang/String;", "framework.build.sdk", true},
          {"SDK_INT", "I", "framework.build.sdk_int", true}}});
    const auto properties_class = classes.RegisterClass(
        {"android/os/SystemProperties", "java/lang/Object",
         {{"get", "(Ljava/lang/String;)Ljava/lang/String;",
           "framework.system_properties.get", true}}, {}});
    const auto secure_class = classes.RegisterClass(
        {"android/provider/Settings$Secure", "java/lang/Object",
         {{"getString",
           "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;",
           "framework.settings_secure.get_string", true}}, {}});
    static_cast<void>(classes.RegisterClass(
        {"android/os/Bundle", "java/lang/Object", {}, {}}));
    static_cast<void>(classes.RegisterClass(
        {"android/view/ViewRoot", "java/lang/Object", {}, {}}));
    const auto uuid_class = classes.RegisterClass(
        {"java/util/UUID", "java/lang/Object",
         {{"randomUUID", "()Ljava/util/UUID;",
           "framework.uuid.random", true},
          {"toString", "()Ljava/lang/String;",
           "framework.uuid.to_string", false}}, {}});

    const auto context = AllocateJniHostObjectIdentity();
    const auto content_resolver = AllocateJniHostObjectIdentity();
    const auto telephony = AllocateJniHostObjectIdentity();
    const auto uuid = AllocateJniHostObjectIdentity();
    objects.Register(context, activity_class);
    objects.Register(content_resolver, content_resolver_class);
    objects.Register(telephony, telephony_class);
    objects.Register(uuid, uuid_class);

    const auto set_string_field = [&](const JniObjectIdentity java_class,
                                      const char* name,
                                      const std::string_view value) {
        fields.SetStatic(
            java_class,
            *classes.GetFieldId(
                java_class, name, "Ljava/lang/String;", true),
            JniValue{PublishGlobalString(
                environment, strings, thread_id, value)});
    };
    set_string_field(context_class, "TELEPHONY_SERVICE", "phone");
    set_string_field(build_class, "BOARD", "generic");
    set_string_field(build_class, "BRAND", "generic");
    set_string_field(build_class, "CPU_ABI", "armeabi-v7a");
    set_string_field(build_class, "DEVICE", "generic");
    set_string_field(build_class, "MANUFACTURER", "OGPlay");
    set_string_field(build_class, "MODEL", config.host_name);
    set_string_field(build_class, "PRODUCT", "generic");
    set_string_field(build_class, "SERIAL", config.installation_id);
    set_string_field(build_class, "TAGS", "release-keys");
    set_string_field(version_class, "RELEASE", "4.4");
    set_string_field(version_class, "SDK", "19");
    fields.SetStatic(
        version_class,
        *classes.GetFieldId(version_class, "SDK_INT", "I", true),
        JniValue{JniInt{19}});

    invocations.RegisterHandler(
        "framework.context.get_system_service",
        [&environment, &strings, telephony](const JniInvocation& invocation) {
            const auto service = ResolveAsciiString(
                environment, strings, invocation, 0);
            return JniValue{service == "phone"
                                ? environment.PublishLocalObject(
                                      invocation.thread_id, telephony)
                                : JniReference{}};
        });
    invocations.RegisterHandler(
        "activity.current",
        [&environment, context](const JniInvocation& invocation) {
            return JniValue{environment.PublishLocalObject(
                invocation.thread_id, context)};
        });
    invocations.RegisterHandler(
        "framework.context.get_content_resolver",
        [&environment,
         content_resolver](const JniInvocation& invocation) {
            return JniValue{environment.PublishLocalObject(
                invocation.thread_id, content_resolver)};
        });
    invocations.RegisterHandler(
        "framework.telephony.get_device_id",
        [&environment, &strings,
         value = config.installation_id](const JniInvocation& invocation) {
            return PublishString(environment, strings, invocation, value);
        });
    invocations.RegisterHandler(
        "framework.system_properties.get",
        [&environment, &strings, serial_value = config.installation_id,
         host_name = config.host_name](const JniInvocation& invocation) {
            const auto key = ResolveAsciiString(
                environment, strings, invocation, 0);
            const auto value = key == "ro.serialno"
                                   ? serial_value
                                   : key == "ro.product.model" ? host_name
                                                                : std::string{};
            return PublishString(environment, strings, invocation, value);
        });
    invocations.RegisterHandler(
        "framework.settings_secure.get_string",
        [&environment, &strings,
         value = config.installation_id](const JniInvocation& invocation) {
            const auto key = ResolveAsciiString(
                environment, strings, invocation, 1);
            if (key != "android_id") return JniValue{JniReference{}};
            return PublishString(environment, strings, invocation, value);
        });
    invocations.RegisterHandler(
        "framework.uuid.random",
        [&environment, uuid](const JniInvocation& invocation) {
            return JniValue{environment.PublishLocalObject(
                invocation.thread_id, uuid)};
        });
    invocations.RegisterHandler(
        "framework.uuid.to_string",
        [&environment, &strings](const JniInvocation& invocation) {
            return PublishString(
                environment, strings, invocation,
                "00000000-0000-4000-8000-000000000001");
        });

    static_cast<void>(object_class);
    static_cast<void>(properties_class);
    static_cast<void>(secure_class);
    return {context_class, content_resolver_class, telephony_class, uuid_class,
            context, content_resolver, telephony, uuid};
}

}  // namespace ogplay::runtime
