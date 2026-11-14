#include "ogplay/hal/host_environment.h"

#include <optional>
#include <stdexcept>
#include <string>

namespace ogplay::hal {
namespace {

// /proc/self/exe names app_process on Android, so the Java host publishes both roots.
constexpr const char* kRuntimeDirectoryVariable = "OGPLAY_ANDROID_RUNTIME_DIR";
constexpr const char* kUserDataDirectoryVariable = "OGPLAY_ANDROID_USER_DATA_DIR";
// language[-COUNTRY], as SDL formats preferred locales on the other platforms.
constexpr const char* kLocaleVariable = "OGPLAY_ANDROID_LOCALE";

}  // namespace

std::filesystem::path HostExecutablePath() {
    return HostExecutableDirectory() / "ogplay";
}

std::filesystem::path HostExecutableDirectory() {
    const auto value = HostEnvironmentValue(kRuntimeDirectoryVariable);
    if (!value.has_value() || value->empty()) {
        throw std::runtime_error("Android host did not publish the runtime directory");
    }
    return std::filesystem::path(*value);
}

std::optional<std::filesystem::path> HostUserDataDirectory() {
    const auto value = HostEnvironmentValue(kUserDataDirectoryVariable);
    if (!value.has_value() || value->empty()) return std::nullopt;
    return std::filesystem::path(*value);
}

std::string HostPreferredLocale() {
    return HostEnvironmentValue(kLocaleVariable).value_or(std::string{});
}

std::string_view HostPlatformName() noexcept {
    return "Android";
}

}  // namespace ogplay::hal
