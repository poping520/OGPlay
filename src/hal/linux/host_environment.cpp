#include "ogplay/hal/host_environment.h"

#include <cstdlib>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include <unistd.h>

namespace ogplay::hal {
namespace {

std::recursive_mutex g_environment_mutex;

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
    std::unique_lock<std::recursive_mutex> lock{g_environment_mutex};
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
    std::vector<char> path(256);
    while (path.size() <= 1024U * 1024U) {
        const auto length = readlink("/proc/self/exe", path.data(), path.size());
        if (length < 0) {
            throw std::runtime_error("failed to locate host executable");
        }
        if (static_cast<std::size_t>(length) < path.size()) {
            return std::filesystem::path(
                       std::string(path.data(),
                                   static_cast<std::size_t>(length)))
                .parent_path();
        }
        path.resize(path.size() * 2U);
    }
    throw std::length_error("host executable path is too long");
}

std::optional<std::string> HostEnvironmentValue(
    const std::string_view name) {
    const std::lock_guard lock(g_environment_mutex);
    const std::string owned_name(name);
    const auto* value = std::getenv(owned_name.c_str());
    return value == nullptr
               ? std::nullopt
               : std::optional<std::string>(std::string(value));
}

}  // namespace ogplay::hal
