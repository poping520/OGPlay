#include "ogplay/runtime/integration/android_guest_call_session.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_object.h"

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
        "platform.unavailable",
        [](const JniInvocation& invocation) -> JniValue {
            throw AndroidGuestCallSessionError(
                "Android guest platform callback is unavailable: " +
                invocation.method.declaration.name +
                invocation.method.declaration.descriptor);
        });
}

}  // namespace ogplay::runtime
