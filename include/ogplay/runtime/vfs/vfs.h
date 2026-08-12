#pragma once

// Android-path virtual filesystem core shared by syscall and framework HLE.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::runtime {

struct VfsOpenOptions final {
    bool read{};
    bool write{};
    bool create{};
    bool truncate{};
};

enum class VfsSource : std::uint8_t { runtime, apk, obb, external };

struct VfsMountEntry final {
    std::string path;
    std::vector<std::byte> contents;
};

using VfsReadOnlyLoader = std::function<std::vector<std::byte>()>;

struct VfsLazyMountEntry final {
    std::string path;
    std::uint64_t size{};
    VfsReadOnlyLoader read_all;
};

enum class VfsSeekWhence : std::uint8_t { begin, current, end };

struct VfsFileInfo final {
    std::uint64_t size{};
    bool writable{};
    VfsSource source{VfsSource::runtime};
};

struct VfsPipeDescriptors final {
    std::int32_t read_descriptor{};
    std::int32_t write_descriptor{};
};

class VfsError final : public std::runtime_error {
public:
    VfsError(std::int32_t error_number, std::string message);
    [[nodiscard]] std::int32_t ErrorNumber() const noexcept {
        return error_number_;
    }

private:
    std::int32_t error_number_{};
};

class VirtualFileSystem final {
public:
    VirtualFileSystem();
    ~VirtualFileSystem();
    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

    void PutFile(std::string_view path, std::span<const std::byte> contents,
                 bool writable);
    void Mount(VfsSource source, std::string_view root,
               std::span<const VfsMountEntry> entries);
    void MountLazyReadOnly(VfsSource source, std::string_view root,
                           std::span<const VfsLazyMountEntry> entries);
    void MountHostDirectory(std::string_view root,
                            const std::filesystem::path& directory);
    // Backing host file for a guest path inside a host-directory mount;
    // nullopt for memory-, APK- or OBB-backed entries.
    [[nodiscard]] std::optional<std::filesystem::path> HostPathFor(
        std::string_view path) const;
    void SetWorkingDirectory(std::string_view path);
    [[nodiscard]] std::optional<std::string> WorkingDirectory() const;
    [[nodiscard]] VfsFileInfo Stat(std::string_view path) const;
    // Immediate children (file and implicit-directory names, sorted,
    // deduplicated) of a directory path; empty when nothing is below it.
    // Directories exist implicitly through the files mounted beneath them.
    [[nodiscard]] std::vector<std::string> ListDirectory(
        std::string_view path) const;
    [[nodiscard]] std::int32_t Open(std::string_view path,
                                    VfsOpenOptions options);
    [[nodiscard]] VfsPipeDescriptors CreatePipe();
    [[nodiscard]] std::size_t Read(std::int32_t descriptor,
                                   std::span<std::byte> destination);
    [[nodiscard]] std::size_t Write(std::int32_t descriptor,
                                    std::span<const std::byte> source);
    [[nodiscard]] std::uint64_t Seek(std::int32_t descriptor,
                                     std::int64_t offset,
                                     VfsSeekWhence whence);
    void Close(std::int32_t descriptor);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
