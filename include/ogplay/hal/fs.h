#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace ogplay::hal {

// Identity comes from the opened object, never from its pathname. It is not
// a content snapshot: in-place host modifications remain externally owned.
struct HostReadFileInfo final {
    std::array<std::uint64_t, 3> identity{};
    std::uint64_t size{};
    bool operator==(const HostReadFileInfo&) const = default;
};

class HostReadFile {
public:
    virtual ~HostReadFile() = default;
    [[nodiscard]] virtual HostReadFileInfo Info() const noexcept = 0;
    [[nodiscard]] virtual std::size_t ReadAt(
        std::uint64_t offset, std::span<std::byte> destination,
        std::stop_token stop = {}) const = 0;
};

// Rejects final-component symlinks/reparse points and non-regular files.
// Native errors are reported as std::system_error; no CRT streams are used.
[[nodiscard]] std::shared_ptr<const HostReadFile> OpenHostReadFile(
    const std::filesystem::path& path);

enum class HostFileType : std::uint8_t { regular, directory, other };

struct HostFileInfo final {
    HostFileType type{HostFileType::other};
    std::uint64_t size{};

    bool operator==(const HostFileInfo&) const = default;
};

class HostFileSystem {
public:
    virtual ~HostFileSystem() = default;
    [[nodiscard]] virtual std::optional<HostFileInfo> Status(
        const std::filesystem::path& path) const = 0;
    [[nodiscard]] virtual std::vector<std::byte> ReadFile(
        const std::filesystem::path& path) const = 0;
    virtual void WriteFile(const std::filesystem::path& path,
                           std::span<const std::byte> contents) = 0;
    virtual void CreateDirectories(const std::filesystem::path& path) = 0;
};

[[nodiscard]] std::unique_ptr<HostFileSystem> CreateStandardHostFileSystem();

}  // namespace ogplay::hal
