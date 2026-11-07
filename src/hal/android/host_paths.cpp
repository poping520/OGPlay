#include "ogplay/hal/host_environment.h"

#include <optional>
#include <stdexcept>
#include <string>

namespace ogplay::hal {
namespace {

// /proc/self/exe names app_process on Android, so the Java host publishes both roots.
constexpr const char* kRuntimeDirectoryVariable = "OGPLAY_ANDROID_RUNTIME_DIR";
constexpr const char* kUserDataDirectoryVariable = "OGPLAY_ANDROID_USER_DATA_DIR";

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

}  // namespace ogplay::hal
