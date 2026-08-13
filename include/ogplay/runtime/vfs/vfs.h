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

class SandboxStore;

struct VfsOpenOptions final {
    bool read{};
    bool write{};
    bool create{};
    bool truncate{};
};

// sandbox: backed by the per-title persistent overlay (ADR-0020).
enum class VfsSource : std::uint8_t { runtime, apk, obb, external, sandbox };

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
    bool is_directory{};
};

struct VfsDirectoryEntry final {
    std::string name;
    bool is_directory{};

    bool operator==(const VfsDirectoryEntry&) const = default;
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
    // Immediate children (sorted, deduplicated) of a directory path; empty
    // when nothing is below it. Directories exist both implicitly, through
    // the files mounted beneath them, and explicitly through CreateDirectory.
    [[nodiscard]] std::vector<VfsDirectoryEntry> ListDirectory(
        std::string_view path) const;

    // Directory and metadata operations (ADR-0020). Pure in-memory
    // semantics; a sandbox attached later persists them at flush points.
    void CreateDirectory(std::string_view path);   // parent must exist
    void RemoveFile(std::string_view path);        // unlink
    void RemoveDirectory(std::string_view path);   // rmdir, -ENOTEMPTY
    void Rename(std::string_view from, std::string_view to);
    [[nodiscard]] std::int32_t Open(std::string_view path,
                                    VfsOpenOptions options);
    // Directory descriptor for getdents64: the child list is snapshotted at
    // open time and consumed by ReadDirectory, so paging is stable even if
    // the guest creates files while walking.
    [[nodiscard]] std::int32_t OpenDirectory(std::string_view path);
    [[nodiscard]] std::vector<VfsDirectoryEntry> ReadDirectory(
        std::int32_t descriptor, std::size_t maximum);
    [[nodiscard]] VfsPipeDescriptors CreatePipe();
    [[nodiscard]] std::size_t Read(std::int32_t descriptor,
                                   std::span<std::byte> destination);
    [[nodiscard]] std::size_t Write(std::int32_t descriptor,
                                    std::span<const std::byte> source);
    [[nodiscard]] std::uint64_t Seek(std::int32_t descriptor,
                                     std::int64_t offset,
                                     VfsSeekWhence whence);
    // Attaches the per-title overlay. Writes inside writable_roots then
    // persist at the flush points; everything else keeps its current
    // read-only base-layer behaviour.
    void AttachSandbox(SandboxStore& store,
                       std::span<const std::string> writable_roots);
    [[nodiscard]] bool SandboxAttached() const;

    void Truncate(std::int32_t descriptor, std::uint64_t size);
    // fsync/fdatasync join here; without an attached sandbox both only
    // check the descriptor, which keeps the call honest rather than absent.
    void Flush(std::int32_t descriptor);
    void FlushAll();
    void Close(std::int32_t descriptor);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
