#include "ogplay/hal/host_environment.h"

#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include <mach-o/dyld.h>

namespace ogplay::hal {
namespace {

std::recursive_mutex& EnvironmentMutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

void ValidateOverrides(
    const std::span<const HostEnvironmentOverride> overrides) {
    std::unordered_set<std::string> names;
    for (const auto& override : overrides) {
        if (override.name.empty() ||
            override.name.find('=') != std::string::npos ||
            override.name.find('\0') != std::string::npos ||
            (override.value.has_value() &&
             override.value->find('\0') != std::string::npos)) {
            throw std::invalid_argument("invalid host environment override");
        }
        if (!names.insert(override.name).second) {
            throw std::invalid_argument(
                "duplicate host environment override");
        }
    }
}

void RestoreEnvironment(
    const std::vector<std::pair<std::string, std::optional<std::string>>>&
        previous) noexcept {
    for (auto iterator = previous.rbegin(); iterator != previous.rend();
         ++iterator) {
        if (iterator->second.has_value()) {
            static_cast<void>(
                setenv(iterator->first.c_str(), iterator->second->c_str(), 1));
        } else {
            static_cast<void>(unsetenv(iterator->first.c_str()));
        }
    }
}

}  // namespace

struct ScopedHostEnvironment::Impl final {
    std::unique_lock<std::recursive_mutex> lock{EnvironmentMutex()};
    std::vector<std::pair<std::string, std::optional<std::string>>> previous;

    ~Impl() {
        RestoreEnvironment(previous);
    }
};

ScopedHostEnvironment::ScopedHostEnvironment(
    const std::span<const HostEnvironmentOverride> overrides) {
    ValidateOverrides(overrides);
    auto impl = std::make_unique<Impl>();
    impl->previous.reserve(overrides.size());
    try {
        for (const auto& override : overrides) {
            const auto* prior = std::getenv(override.name.c_str());
            impl->previous.emplace_back(
                override.name,
                prior == nullptr
                    ? std::nullopt
                    : std::optional<std::string>(std::string(prior)));
            int result{};
            if (override.value.has_value()) {
                result = setenv(override.name.c_str(),
                                override.value->c_str(), 1);
            } else {
                result = unsetenv(override.name.c_str());
            }
            if (result != 0) {
                throw std::runtime_error(
                    "failed to override host environment");
            }
        }
    } catch (...) {
        RestoreEnvironment(impl->previous);
        impl->previous.clear();
        throw;
    }
    impl_ = std::move(impl);
}

ScopedHostEnvironment::~ScopedHostEnvironment() = default;

std::filesystem::path HostExecutableDirectory() {
    std::uint32_t size{1};
    char placeholder{};
    if (_NSGetExecutablePath(&placeholder, &size) != -1 || size <= 1) {
        throw std::runtime_error("failed to size host executable path");
    }
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size) != 0) {
        throw std::runtime_error("failed to locate host executable");
    }
    return std::filesystem::canonical(std::filesystem::path(path.data()))
        .parent_path();
}

std::optional<std::string> HostEnvironmentValue(
    const std::string_view name) {
    const std::lock_guard lock(EnvironmentMutex());
    const std::string owned_name(name);
    const auto* value = std::getenv(owned_name.c_str());
    return value == nullptr
               ? std::nullopt
               : std::optional<std::string>(std::string(value));
}

std::optional<std::filesystem::path> HostUserDataDirectory() {
    const auto home = HostEnvironmentValue("HOME");
    if (!home.has_value() || home->empty()) return std::nullopt;
    return std::filesystem::path(*home) / "Library" / "Application Support" /
           "OGPlay";
}

}  // namespace ogplay::hal
