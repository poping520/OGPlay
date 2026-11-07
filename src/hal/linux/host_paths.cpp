#include "ogplay/hal/host_environment.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

namespace ogplay::hal {

std::filesystem::path HostExecutablePath() {
    std::vector<char> path(256);
    while (path.size() <= 1024U * 1024U) {
        const auto length = readlink("/proc/self/exe", path.data(), path.size());
        if (length < 0) {
            throw std::runtime_error("failed to locate host executable");
        }
        if (static_cast<std::size_t>(length) < path.size()) {
            return std::filesystem::path(std::string(path.data(), static_cast<std::size_t>(length)));
        }
        path.resize(path.size() * 2U);
    }
    throw std::length_error("host executable path is too long");
}

std::filesystem::path HostExecutableDirectory() {
    return HostExecutablePath().parent_path();
}

std::optional<std::filesystem::path> HostUserDataDirectory() {
    const auto xdg = HostEnvironmentValue("XDG_DATA_HOME");
    if (xdg.has_value() && !xdg->empty()) {
        return std::filesystem::path(*xdg) / "ogplay";
    }
    const auto home = HostEnvironmentValue("HOME");
    if (!home.has_value() || home->empty()) return std::nullopt;
    return std::filesystem::path(*home) / ".local" / "share" / "ogplay";
}

}  // namespace ogplay::hal
