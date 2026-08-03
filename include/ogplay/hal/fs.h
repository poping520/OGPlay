#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace ogplay::hal {

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
