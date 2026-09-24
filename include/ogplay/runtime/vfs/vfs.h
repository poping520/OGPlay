#pragma once

// Android-path virtual filesystem core shared by syscall and framework HLE.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <map>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ogplay::runtime {

class SandboxStore;

class VfsReadLease {
public:
    virtual ~VfsReadLease() = default;
    [[nodiscard]] virtual std::uint64_t Size() const noexcept = 0;
    [[nodiscard]] virtual std::size_t ReadAt(
        std::uint64_t offset, std::span<std::byte> destination,
        std::stop_token stop = {}) const = 0;
};

struct VfsOpenOptions final {
    bool read{};
    bool write{};
    bool create{};
    bool truncate{};
    bool directory{};
};

// sandbox: backed by the per-title persistent overlay (ADR-0020).
enum class VfsSource : std::uint8_t { runtime, apk, obb, external, sandbox };

struct VfsMountEntry final {
    std::string path;
    std::vector<std::byte> contents;
};

using VfsReadOnlyLoader = std::function<std::vector<std::byte>()>;
class VfsReadAtLoader final {
public:
    VfsReadAtLoader() = default;

    template <class Loader>
    VfsReadAtLoader(Loader loader) {
        if constexpr (std::is_invocable_r_v<std::size_t, Loader&,
                          std::uint64_t, std::span<std::byte>, std::stop_token>) {
            loader_ = std::move(loader);
        } else {
            static_assert(std::is_invocable_r_v<std::size_t, Loader&,
                              std::uint64_t, std::span<std::byte>>);
            loader_ = [loader = std::move(loader)](
                          const std::uint64_t offset,
                          const std::span<std::byte> destination,
                          const std::stop_token stop) mutable {
                if (stop.stop_requested()) return std::size_t{};
                return loader(offset, destination);
            };
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(loader_);
    }
    [[nodiscard]] std::size_t operator()(
        const std::uint64_t offset, const std::span<std::byte> destination,
        const std::stop_token stop = {}) const {
        return loader_(offset, destination, stop);
    }

private:
    std::function<std::size_t(std::uint64_t, std::span<std::byte>,
                              std::stop_token)> loader_;
};

struct VfsLazyMountEntry final {
    std::string path;
    std::uint64_t size{};
    VfsReadOnlyLoader read_all;
    // Optional bounded reader. When present, ordinary reads do not invoke
    // read_all or allocate the complete file.
    VfsReadAtLoader read_at;
};

enum class VfsSeekWhence : std::uint8_t { begin, current, end };

struct VfsFileInfo final {
    std::uint64_t size{};
    bool writable{};
    VfsSource source{VfsSource::runtime};
    bool is_directory{};
    std::uint64_t generation{};
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

struct VfsIoStatistics final {
    std::uint64_t backing_read_bytes{};
    std::uint64_t full_materialized_bytes{};
    std::uint64_t resource_memory_bytes{};
    std::uint64_t resource_memory_high_water{};
    std::uint64_t lease_snapshot_bytes{};
    std::uint64_t lease_snapshot_high_water{};
};

struct VfsDescriptorSnapshot final {
    std::int32_t fd{};
    std::optional<std::uint64_t> node_id, offset;
    bool readable{}, writable{}, busy{};
};
struct VfsMountSnapshot final { std::string root; VfsSource source{}; };
struct VfsSnapshot final {
    VfsIoStatistics io;
    std::vector<VfsMountSnapshot> mounts;
    std::vector<VfsDescriptorSnapshot> descriptors;
    std::size_t total_mounts{}, total_descriptors{};
    bool partial{}, sandbox_attached{};
    std::uint64_t flushes{};
};

struct VfsConfig final {
    // One aggregate budget for every retained resource buffer, including
    // writable lease snapshots and decompressed archive cache blocks.
    std::uint64_t resource_memory_budget_bytes{128ULL * 1024ULL * 1024ULL};
};

class VfsResourceReservation final {
public:
    ~VfsResourceReservation();
    VfsResourceReservation(const VfsResourceReservation&) = delete;
    VfsResourceReservation& operator=(const VfsResourceReservation&) = delete;

private:
    friend class VirtualFileSystem;
    explicit VfsResourceReservation(std::function<void()> release);
    std::function<void()> release_;
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
    explicit VirtualFileSystem(VfsConfig config = {});
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
    void MountHostFile(VfsSource source, std::string_view root,
                       const std::filesystem::path& file);
    // Canonicalizes a path prefix onto an already mounted namespace. Both
    // spellings then address the same nodes and overlay.
    void AddPathAlias(std::string_view alias, std::string_view target);
    void SetWorkingDirectory(std::string_view path);
    [[nodiscard]] std::optional<std::string> WorkingDirectory() const;
    // Returns the normalized node identity used internally by all VFS file
    // operations, including path aliases.
    [[nodiscard]] std::string CanonicalPath(std::string_view path) const;
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
    [[nodiscard]] VfsFileInfo DescriptorInfo(std::int32_t descriptor) const;
    [[nodiscard]] VfsPipeDescriptors CreatePipe();
    [[nodiscard]] std::size_t Read(std::int32_t descriptor,
                                   std::span<std::byte> destination);
    [[nodiscard]] std::size_t ReadAt(std::int32_t descriptor,
                                     std::uint64_t offset,
                                     std::span<std::byte> destination);
    [[nodiscard]] std::size_t Write(std::int32_t descriptor,
                                    std::span<const std::byte> source);
    [[nodiscard]] std::size_t WriteAt(std::int32_t descriptor,
                                      std::uint64_t offset,
                                      std::span<const std::byte> source);
    [[nodiscard]] std::shared_ptr<const VfsReadLease> CaptureReadLease(
        std::int32_t descriptor, std::uint64_t offset,
        std::uint64_t length = std::numeric_limits<std::uint64_t>::max());
    [[nodiscard]] std::uint64_t Seek(std::int32_t descriptor,
                                     std::int64_t offset,
                                     VfsSeekWhence whence);
    // Attaches the per-title overlay. Writes inside writable_roots then
    // persist at the flush points; everything else keeps its current
    // read-only base-layer behaviour.
    void AttachSandbox(SandboxStore& store,
                       std::span<const std::string> writable_roots);
    [[nodiscard]] bool SandboxAttached() const;
    [[nodiscard]] VfsIoStatistics IoStatistics() const;
    [[nodiscard]] std::optional<VfsSnapshot> TrySnapshot() const;
    [[nodiscard]] std::optional<std::uint64_t> TryDescriptorNode(std::int32_t descriptor) const;
    // Reserves from the same aggregate memory budget used by writable lease
    // snapshots. Callers must reserve before allocating and retain the token
    // for as long as the allocation is reachable.
    [[nodiscard]] std::shared_ptr<const VfsResourceReservation>
    ReserveResourceMemory(std::uint64_t bytes);

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
