#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ogplay::hal {

struct HostEnvironmentOverride final {
    std::string name;
    std::optional<std::string> value;
};

class ScopedHostEnvironment final {
public:
    explicit ScopedHostEnvironment(
        std::span<const HostEnvironmentOverride> overrides);
    ~ScopedHostEnvironment();

    ScopedHostEnvironment(const ScopedHostEnvironment&) = delete;
    ScopedHostEnvironment& operator=(const ScopedHostEnvironment&) = delete;
    ScopedHostEnvironment(ScopedHostEnvironment&&) = delete;
    ScopedHostEnvironment& operator=(ScopedHostEnvironment&&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::filesystem::path HostExecutableDirectory();
// Per-platform root for this user's application data (roadmap 08). Empty
// when the host environment does not say where it belongs, which callers
// must report rather than guess.
//   Windows  %APPDATA%\\OGPlay
//   macOS    ~/Library/Application Support/OGPlay
//   Linux    $XDG_DATA_HOME/ogplay, else ~/.local/share/ogplay
[[nodiscard]] std::optional<std::filesystem::path> HostUserDataDirectory();
[[nodiscard]] std::optional<std::string> HostEnvironmentValue(
    std::string_view name);

}  // namespace ogplay::hal
