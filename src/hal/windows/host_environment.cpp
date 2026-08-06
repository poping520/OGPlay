#include "ogplay/hal/host_environment.h"

#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

std::optional<std::string> ReadEnvironment(const std::string& name) {
    SetLastError(ERROR_SUCCESS);
    const auto size = GetEnvironmentVariableA(name.c_str(), nullptr, 0);
    if (size == 0) {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
            return std::nullopt;
        }
        return std::string{};
    }
    std::string value(size, '\0');
    const auto written =
        GetEnvironmentVariableA(name.c_str(), value.data(), size);
    if (written + 1U != size) {
        throw std::runtime_error("failed to read host environment");
    }
    value.resize(written);
    return value;
}

void RestoreEnvironment(
    const std::vector<std::pair<std::string, std::optional<std::string>>>&
        previous) noexcept {
    for (auto iterator = previous.rbegin(); iterator != previous.rend();
         ++iterator) {
        static_cast<void>(SetEnvironmentVariableA(
            iterator->first.c_str(),
            iterator->second.has_value() ? iterator->second->c_str()
                                         : nullptr));
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
            impl->previous.emplace_back(override.name,
                                        ReadEnvironment(override.name));
            if (!SetEnvironmentVariableA(override.name.c_str(),
                                         override.value.has_value()
                                             ? override.value->c_str()
                                             : nullptr)) {
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
    std::vector<wchar_t> path(256);
    while (path.size() <= 32768U) {
        SetLastError(ERROR_SUCCESS);
        const auto length = GetModuleFileNameW(
            nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            throw std::runtime_error("failed to locate host executable");
        }
        if (length < path.size() ||
            (length == path.size() &&
             GetLastError() != ERROR_INSUFFICIENT_BUFFER)) {
            return std::filesystem::path(
                       std::wstring(path.data(), length))
                .parent_path();
        }
        path.resize(path.size() * 2U);
    }
    throw std::length_error("host executable path is too long");
}

std::optional<std::string> HostEnvironmentValue(
    const std::string_view name) {
    const std::lock_guard lock(g_environment_mutex);
    return ReadEnvironment(std::string(name));
}

}  // namespace ogplay::hal
