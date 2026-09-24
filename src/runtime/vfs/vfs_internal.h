#pragma once

// Internal VFS state shared by the core translation unit (vfs.cpp) and the
// sandbox overlay one (vfs_sandbox.cpp). Not installed; include order is
// private to runtime/vfs.

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {

inline constexpr std::int32_t kEnoent = 2;
inline constexpr std::int32_t kEio = 5;
inline constexpr std::int32_t kEbadf = 9;
inline constexpr std::int32_t kEacces = 13;
inline constexpr std::int32_t kEexist = 17;
inline constexpr std::int32_t kEnotdir = 20;
inline constexpr std::int32_t kEisdir = 21;
inline constexpr std::int32_t kEinval = 22;
inline constexpr std::int32_t kEfbig = 27;
inline constexpr std::int32_t kEspipe = 29;
inline constexpr std::int32_t kEnospc = 28;
inline constexpr std::int32_t kEnotempty = 39;
inline constexpr std::int32_t kEcanceled = 125;

[[nodiscard]] std::string NormalizeAbsolutePath(std::string_view path,
                                                bool allow_root);
[[nodiscard]] std::string NormalizePath(std::string_view path);
[[nodiscard]] std::string ResolvePath(
    std::string_view path,
    const std::optional<std::string>& working_directory,
    const std::map<std::string, std::string, std::less<>>& aliases);
struct File final {
    File() = default;
    File(std::vector<std::byte> initial_contents, std::uint64_t initial_size,
         VfsReadOnlyLoader initial_read_all, VfsReadAtLoader initial_read_at,
         bool initial_writable, VfsSource initial_source, bool initial_dirty,
         std::string initial_overlay_path)
        : contents(std::move(initial_contents)), size(initial_size),
          read_all(std::move(initial_read_all)),
          read_at(std::move(initial_read_at)), writable(initial_writable),
          source(initial_source), dirty(initial_dirty),
          overlay_path(std::move(initial_overlay_path)) {}

    std::shared_ptr<std::mutex> mutex{std::make_shared<std::mutex>()};
    // Stable for the node lifetime. Paths may be removed or rebound; caches
    // and leases must never use a pathname as content identity.
    std::uint64_t node_id{};
    std::vector<std::byte> contents;
    std::uint64_t size{};
    VfsReadOnlyLoader read_all;
    VfsReadAtLoader read_at;
    // Retained only when a writable lazy backing was converted to owned
    // memory; the reservation is acquired before read_all allocates.
    std::vector<std::shared_ptr<const VfsResourceReservation>>
        materialized_reservations;
    std::uint64_t materialized_reserved_bytes{};
    bool writable{};
    VfsSource source{VfsSource::runtime};
    // Sandbox bookkeeping: set once the node belongs to the overlay, so a
    // rename carries the persistence identity with the node.
    bool dirty{};
    std::string overlay_path;
    // Bytes already counted by SandboxStore::UsedBytes. Dirty quota checks
    // replace this amount with the node's current in-memory size.
    std::uint64_t persisted_size{};
    std::uint64_t generation{1U};
};

// A descriptor opened on a directory: the children are snapshotted so
// getdents64 paging stays stable across calls.
struct OpenDirectoryState final {
    std::vector<VfsDirectoryEntry> entries;
    std::size_t cursor{};
    VfsFileInfo info;
};

struct OpenFile final {
    OpenFile(std::shared_ptr<File> initial_file, std::uint64_t initial_offset,
             bool initial_readable, bool initial_writable,
             std::shared_ptr<OpenDirectoryState> initial_directory,
             bool initial_pipe = false)
        : file(std::move(initial_file)), offset(initial_offset),
          readable(initial_readable), writable(initial_writable),
          directory(std::move(initial_directory)), pipe(initial_pipe) {}

    mutable std::mutex mutex;
    std::shared_ptr<File> file;
    std::uint64_t offset{};
    bool readable{};
    bool writable{};
    std::shared_ptr<OpenDirectoryState> directory;
    bool pipe{};
};

struct ResourceBudget final {
    std::mutex mutex;
    std::uint64_t used{};
    std::uint64_t limit{128ULL * 1024ULL * 1024ULL};
    std::uint64_t high_water{};
    std::uint64_t snapshot_used{};
    std::uint64_t snapshot_high_water{};
};

class VirtualFileSystem::Impl final {
public:
    explicit Impl(const VfsConfig config) {
        if (config.resource_memory_budget_bytes == 0U)
            throw VfsError(kEinval, "VFS resource budget must be positive");
        resource_budget_->limit = config.resource_memory_budget_bytes;
    }
    // ---- mounts and files (vfs.cpp) -------------------------------------
    void PutFile(std::string_view path, std::span<const std::byte> contents,
                 bool writable);
    void Mount(VfsSource source, std::string_view root,
               std::span<const VfsMountEntry> entries);
    void MountLazy(VfsSource source, std::string_view root,
                   std::span<const VfsLazyMountEntry> entries, bool writable);
    void MountHostDirectory(std::string_view root,
                            const std::filesystem::path& directory);
    void MountHostFile(VfsSource source, std::string_view root,
                       const std::filesystem::path& file);
    void AddPathAlias(std::string_view alias, std::string_view target);
    void SetWorkingDirectory(std::string_view path);
    [[nodiscard]] std::optional<std::string> WorkingDirectory() const;
    [[nodiscard]] std::string CanonicalPath(std::string_view path) const;
    [[nodiscard]] std::int32_t Open(std::string_view path,
                                    VfsOpenOptions options);
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
        std::uint64_t length);
    [[nodiscard]] std::uint64_t Seek(std::int32_t descriptor,
                                     std::int64_t offset,
                                     VfsSeekWhence whence);
    void Close(std::int32_t descriptor);

    // ---- directories, metadata and the sandbox (vfs_sandbox.cpp) --------
    [[nodiscard]] VfsFileInfo Stat(std::string_view path) const;
    [[nodiscard]] std::vector<VfsDirectoryEntry> ListDirectory(
        std::string_view path) const;
    void CreateDirectory(std::string_view path);
    void RemoveFile(std::string_view path);
    void RemoveDirectory(std::string_view path);
    void Rename(std::string_view from, std::string_view to);
    void Truncate(std::int32_t descriptor, std::uint64_t size);
    void Flush(std::int32_t descriptor);
    void FlushAll();
    void AttachSandbox(SandboxStore& store,
                       std::span<const std::string> writable_roots);
    [[nodiscard]] bool SandboxAttached() const;
    [[nodiscard]] VfsIoStatistics IoStatistics() const;
    [[nodiscard]] std::optional<VfsSnapshot> TrySnapshot() const;
    [[nodiscard]] std::shared_ptr<const VfsResourceReservation>
    ReserveResourceMemory(std::uint64_t bytes, bool snapshot);

    void Materialize(File& file);

    // Callers hold mutex_ for every helper below.
    [[nodiscard]] bool IsDirectoryLocked(const std::string& path) const;
    [[nodiscard]] VfsFileInfo DirectoryInfoLocked(
        const std::string& path) const;
    // True when the path sits under a writable namespace root, which is the
    // only place the overlay accepts writes.
    [[nodiscard]] bool IsWritableNamespaceLocked(const std::string& path) const;
    void MarkOverlayLocked(const std::string& path, File& file);
    void FlushFileLocked(File& file);
    void RequireSandboxQuotaLocked(const File& target,
                                   std::uint64_t prospective_size) const;
    // The only place a node's size or dirty flag may change: it keeps the
    // running dirty-byte aggregate consistent so quota checks stay O(1)
    // instead of rescanning every file per write.
    void SetNodeSizeDirtyLocked(File& file, std::uint64_t size, bool dirty);
    void PersistDirectoryLocked(const std::string& path);
    void PersistRemovalLocked(const std::string& path);
    [[nodiscard]] std::int32_t AllocateDescriptor() const;
    [[nodiscard]] std::shared_ptr<OpenFile> FindDescriptor(
        std::int32_t descriptor) const;

    mutable std::mutex mutex_;
    std::optional<std::string> working_directory_;
    std::map<std::string, std::string, std::less<>> aliases_;
    std::map<std::string, std::shared_ptr<File>, std::less<>> files_;
    // Directories created explicitly; implicit ones come from files_.
    std::set<std::string, std::less<>> directories_;
    std::vector<VfsMountSnapshot> mounts_;
    std::map<std::int32_t, std::shared_ptr<OpenFile>> descriptors_;
    // Sandbox overlay (ADR-0020). Absent until AttachSandbox.
    SandboxStore* sandbox_{};
    std::vector<std::string> writable_roots_;
    // Paths the guest deleted that the read-only base layer still provides.
    std::set<std::string, std::less<>> tombstones_;
    // Σ over dirty overlay nodes of (size - persisted_size), maintained by
    // SetNodeSizeDirtyLocked and FlushFileLocked. Sizes are bounded by the
    // sandbox quota and host memory, so a signed 64-bit delta cannot wrap.
    std::int64_t dirty_overlay_delta_{};
    std::shared_ptr<ResourceBudget> resource_budget_{
        std::make_shared<ResourceBudget>()};
    std::uint64_t next_node_id_{1U};
    std::atomic_uint64_t flushes_{};
    std::atomic_uint64_t backing_read_bytes_{};
    std::atomic_uint64_t full_materialized_bytes_{};
};

}  // namespace ogplay::runtime
