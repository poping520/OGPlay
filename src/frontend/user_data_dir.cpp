#include "ogplay/frontend/user_data_dir.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "ogplay/core/encoding.h"
#include "ogplay/hal/host_environment.h"

namespace ogplay::frontend {
namespace {

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] std::runtime_error PortableRootError(const std::filesystem::path& root, std::string_view operation,
                                                   const std::error_code& error) {
    return std::runtime_error("Cannot " + std::string(operation) + " for the portable user-data directory '" +
                              PathUtf8(root) + "': " + error.message() +
                              ". Move OGPlay to a writable folder or delete portable.txt to use %APPDATA%\\OGPlay.");
}

[[nodiscard]] std::error_code ProbeIoError() {
    return errno ? std::error_code(errno, std::generic_category()) : std::make_error_code(std::errc::io_error);
}

}  // namespace

std::optional<UserDataLocation> ResolveUserDataLocation(const std::filesystem::path& executable_directory,
                                                        bool portable_supported,
                                                        const std::optional<std::filesystem::path>& platform_root) {
    if (portable_supported) {
        const auto marker = executable_directory / "portable.txt";
        std::error_code error;
        if (std::filesystem::is_regular_file(marker, error))
            return UserDataLocation{executable_directory / "userdata", true};
        if (error && error != std::errc::no_such_file_or_directory && error != std::errc::not_a_directory &&
            error != std::errc::too_many_symbolic_link_levels) {
            throw std::runtime_error("Cannot check portable mode marker '" + PathUtf8(marker) +
                                     "': " + error.message());
        }
    }
    if (!platform_root) return std::nullopt;
    return UserDataLocation{*platform_root, false};
}

void EnsureWritableUserDataRoot(const std::filesystem::path& root) {
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) throw PortableRootError(root, "create a directory", error);

    // GUI, CLI and concurrent launches may all probe the same root.
    std::array<std::byte, 16> token{};
    hal::FillSecureRandom(token);
    const auto probe = root / (".ogplay-write-test-" + core::EncodeHex(token));
    errno = 0;
    std::ofstream output(probe, std::ios::binary);
    if (!output) throw PortableRootError(root, "create a write probe", ProbeIoError());

    errno = 0;
    output.put('\0');
    auto write_error = output ? std::error_code{} : ProbeIoError();
    errno = 0;
    output.close();
    if (!output && !write_error) write_error = ProbeIoError();

    std::filesystem::remove(probe, error);
    if (write_error) throw PortableRootError(root, "write a probe file", write_error);
    if (error) throw PortableRootError(root, "remove the write probe", error);
}

std::optional<UserDataLocation> HostUserDataLocation() {
    const bool portable_supported = hal::HostSupportsPortableMode();
    auto location =
        ResolveUserDataLocation(portable_supported ? hal::HostExecutableDirectory() : std::filesystem::path{},
                                portable_supported, hal::HostUserDataDirectory());
    if (location && location->portable) EnsureWritableUserDataRoot(location->root);
    return location;
}

std::optional<std::filesystem::path> UserDataDirectory() {
    auto location = HostUserDataLocation();
    if (!location) return std::nullopt;
    return std::move(location->root);
}

std::optional<std::filesystem::path> DefaultSandboxRoot() {
    const auto base = UserDataDirectory();
    if (!base.has_value()) return std::nullopt;
    return *base / "sandbox";
}

std::optional<std::filesystem::path> DefaultLibraryRoot() {
    const auto base = UserDataDirectory();
    if (!base.has_value()) return std::nullopt;
    return *base / "library";
}

}  // namespace ogplay::frontend
