#pragma once

// Internal VFS state shared by the core translation unit (vfs.cpp) and the
// sandbox overlay one (vfs_sandbox.cpp). Not installed; include order is
// private to runtime/vfs.

#include <cstdint>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
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
#include "host_file_backing.h"

namespace ogplay::runtime {

inline constexpr std::int32_t kEnoent = 2;
inline constexpr std::int32_t kEintr = 4;
inline constexpr std::int32_t kEio = 5;
inline constexpr std::int32_t kEbadf = 9;
inline constexpr std::int32_t kEagain = 11;
inline constexpr std::int32_t kEacces = 13;
inline constexpr std::int32_t kEbusy = 16;
inline constexpr std::int32_t kEexist = 17;
inline constexpr std::int32_t kEnotdir = 20;
inline constexpr std::int32_t kEisdir = 21;
inline constexpr std::int32_t kEinval = 22;
inline constexpr std::int32_t kEfbig = 27;
inline constexpr std::int32_t kEspipe = 29;
inline constexpr std::int32_t kEpipe = 32;
inline constexpr std::int32_t kEmfile = 24;
inline constexpr std::int32_t kEnospc = 28;
inline constexpr std::int32_t kErofs = 30;
inline constexpr std::int32_t kEnotempty = 39;
inline constexpr std::int32_t kEcanceled = 125;

[[nodiscard]] constexpr bool IsSystemPartitionPath(std::string_view path) {
    return path == "/system" || path.starts_with("/system/");
}

[[nodiscard]] std::string NormalizeAbsolutePath(std::string_view path, bool allow_root);
[[nodiscard]] std::string NormalizePath(std::string_view path);
// Returns the absolute spelled path when it names canonical up to ASCII case.
[[nodiscard]] std::string PreserveSpelling(std::string_view canonical, std::string_view spelled);
[[nodiscard]] std::string ResolvePath(std::string_view path, const std::optional<std::string>& working_directory,
                                      const std::map<std::string, std::string, std::less<>>& aliases);

struct VfsGeneratedFileState final {
    std::mutex mutex;
    std::atomic_bool active{true};
    VfsReadOnlyLoader provider;
    std::uint64_t maximum_bytes{};
};

struct VfsCharacterDeviceState final {
    std::mutex mutex;
    std::atomic_bool active{true};
    VfsCharacterReader reader;
    std::uint64_t device_number{};
};

struct File final {
    File() = default;

    File(std::vector<std::byte> initial_contents, std::uint64_t initial_size, VfsReadOnlyLoader initial_read_all,
         VfsReadAtLoader initial_read_at, bool initial_writable, VfsSource initial_source, bool initial_dirty,
         std::string initial_overlay_path)
        : contents(std::move(initial_contents)), size(initial_size), read_all(std::move(initial_read_all)),
          read_at(std::move(initial_read_at)), writable(initial_writable), source(initial_source), dirty(initial_dirty),
          overlay_path(std::move(initial_overlay_path)) {}

    std::shared_ptr<std::mutex> mutex{std::make_shared<std::mutex>()};
    // Stable for the node lifetime. Paths may be removed or rebound; caches
    // and leases must never use a pathname as content identity.
    std::uint64_t node_id{};
    // Generated snapshots have independent node IDs but share their provider's inode.
    std::uint32_t inode{};
    // Guarded by the metadata lock. Retained after unlink for open descriptors.
    std::string canonical_path;
    std::string spelling;
    // Accessed only under the VFS metadata lock; weak tokens do not pin nodes.
    std::vector<std::weak_ptr<const std::function<void()>>> mapping_readers;
    std::shared_ptr<VfsGeneratedFileState> generated;
    std::shared_ptr<VfsCharacterDeviceState> character_device;
    std::vector<std::byte> contents;
    std::uint64_t size{};
    VfsReadOnlyLoader read_all;
    VfsReadAtLoader read_at;
    std::shared_ptr<HostFileBacking> host_backing;
    // Immutable after publication; ordinary/injected nodes must not acquire
    // the node IO lock merely to open another descriptor.
    bool host_mounted{};
    // Retained only when a writable lazy backing was converted to owned
    // memory; the reservation is acquired before read_all allocates.
    std::vector<std::shared_ptr<const VfsResourceReservation>> materialized_reservations;
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
    VfsFileMetadata metadata;
};

// A descriptor opened on a directory: the children are snapshotted so
// getdents64 paging stays stable across calls.
struct OpenDirectoryState final {
    std::vector<VfsDirectoryEntry> entries;
    std::size_t cursor{};
    VfsFileInfo info;
    std::string path;
    bool system_partition{};
    std::string spelling;  // Guest spelling of path, for fchdir/getcwd.
    bool unlinked{};       // Guarded by the VFS namespace mutex.
};

// Wakes pollers and blocked pipe IO. Shared so pipe ends that outlive a
// descriptor table entry can still signal their peers when they retire.
struct PipeReadiness final {
    std::mutex mutex;
    std::condition_variable changed;
    std::uint64_t generation{};

    void Notify() {
        {
            std::scoped_lock lock(mutex);
            ++generation;
        }
        changed.notify_all();
    }
};

// Bounded pipe buffer. readers/writers count open descriptions (not
// descriptor numbers), so the counts stay right across tables and dup.
struct PipeState final {
    std::mutex mutex;
    std::deque<std::byte> buffer;
    std::size_t readers{};
    std::size_t writers{};
};

// One per pipe open description; retiring it updates the end count.
class PipeEnd final {
public:
    PipeEnd(std::shared_ptr<PipeState> state, std::shared_ptr<PipeReadiness> readiness, bool reader)
        : state_(std::move(state)), readiness_(std::move(readiness)), reader_(reader) {
        std::scoped_lock lock(state_->mutex);
        ++(reader_ ? state_->readers : state_->writers);
    }

    ~PipeEnd() {
        {
            std::scoped_lock lock(state_->mutex);
            --(reader_ ? state_->readers : state_->writers);
        }
        readiness_->Notify();
    }

    PipeEnd(const PipeEnd&) = delete;
    PipeEnd& operator=(const PipeEnd&) = delete;

private:
    std::shared_ptr<PipeState> state_;
    std::shared_ptr<PipeReadiness> readiness_;
    bool reader_{};
};

struct OpenFile final {
    OpenFile(std::shared_ptr<File> initial_file, std::uint64_t initial_offset, bool initial_readable,
             bool initial_writable, std::shared_ptr<OpenDirectoryState> initial_directory, bool initial_pipe = false)
        : file(std::move(initial_file)), offset(initial_offset), readable(initial_readable), writable(initial_writable),
          directory(std::move(initial_directory)), pipe(initial_pipe) {}

    mutable std::mutex mutex;
    std::shared_ptr<File> file;
    std::uint64_t offset{};
    bool readable{};
    bool writable{};
    std::shared_ptr<OpenDirectoryState> directory;
    bool append{};
    bool non_blocking{};
    bool large_file{};
    bool no_follow{};
    bool pipe{};
    std::uint32_t owner_id{};
    bool system_partition{};
    std::shared_ptr<const hal::HostReadFile> host_pin;
    // Set for pipe ends: IO goes to the shared buffer, never to `file`.
    std::shared_ptr<PipeState> pipe_state;
    std::unique_ptr<PipeEnd> pipe_end;
    // Set for stream endpoints; `file` is then an empty placeholder node.
    std::shared_ptr<const VfsStreamEndpoint> endpoint;
};

struct DescriptorTable final {
    std::map<std::int32_t, std::shared_ptr<OpenFile>> entries;
    std::set<std::int32_t> close_on_exec;
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
        if (config.resource_memory_budget_bytes == 0U) throw VfsError(kEinval, "VFS resource budget must be positive");
        resource_budget_->limit = config.resource_memory_budget_bytes;
        host_pool_ = std::make_shared<HostFilePool>(config.host_file_handle_budget);
    }

    // ---- mounts and files (vfs.cpp) -------------------------------------
    void RegisterGeneratedReadOnly(std::string_view path, std::shared_ptr<VfsGeneratedFileState> state);
    void RegisterReadOnlyCharacterDevice(std::string_view path, std::shared_ptr<VfsCharacterDeviceState> state);
    void PutFile(std::string_view path, std::span<const std::byte> contents, bool writable);
    void PutReadOnlyFile(std::string_view path, std::uint64_t size, VfsReadAtLoader read_at);
    void Mount(VfsSource source, std::string_view root, std::span<const VfsMountEntry> entries);
    void MountLazy(VfsSource source, std::string_view root, std::span<const VfsLazyMountEntry> entries, bool writable,
                   std::span<const std::shared_ptr<HostFileBacking>> host_backings = {},
                   std::span<const std::string> empty_directories = {});
    void MountHostDirectory(std::string_view root, const std::filesystem::path& directory);
    void MountHostFile(VfsSource source, std::string_view root, const std::filesystem::path& file);
    void AddPathAlias(std::string_view alias, std::string_view target);
    void SetWorkingDirectory(std::string_view path);
    void SetWorkingDirectory(std::int32_t descriptor);
    [[nodiscard]] std::optional<std::string> WorkingDirectory() const;
    [[nodiscard]] std::string CanonicalPath(std::string_view path) const;
    // Returns an existing file node's published spelling after normal path
    // resolution. Missing files throw ENOENT; directories throw EISDIR.
    [[nodiscard]] std::string FileSpelling(std::string_view path) const;
    [[nodiscard]] std::int32_t Open(std::string_view path, VfsOpenOptions options);
    [[nodiscard]] std::int32_t OpenDirectory(std::string_view path, bool large_file = false, bool no_follow = false);
    [[nodiscard]] std::vector<VfsDirectoryEntry> ReadDirectory(std::int32_t descriptor, std::size_t maximum);
    [[nodiscard]] VfsFileInfo DescriptorInfo(std::int32_t descriptor) const;
    [[nodiscard]] VfsOpenStatus DescriptorStatus(std::int32_t descriptor) const;
    [[nodiscard]] VfsPipeDescriptors CreatePipe(bool non_blocking);
    [[nodiscard]] std::optional<std::uint32_t> PipePollEvents(std::int32_t descriptor) const;
    [[nodiscard]] std::uint64_t PipeReadinessGeneration() const;
    void WaitPipeReadiness(std::uint64_t observed, std::chrono::nanoseconds timeout) const;
    void NotifyPipeReadiness() const;
    [[nodiscard]] std::size_t Read(std::int32_t descriptor, std::span<std::byte> destination);
    [[nodiscard]] std::size_t ReadAt(std::int32_t descriptor, std::uint64_t offset, std::span<std::byte> destination);
    [[nodiscard]] std::size_t Write(std::int32_t descriptor, std::span<const std::byte> source);
    [[nodiscard]] std::size_t WriteAt(std::int32_t descriptor, std::uint64_t offset, std::span<const std::byte> source);
    [[nodiscard]] std::shared_ptr<const VfsReadLease> CaptureReadLease(std::int32_t descriptor, std::uint64_t offset,
                                                                       std::uint64_t length);
    [[nodiscard]] std::uint64_t Seek(std::int32_t descriptor, std::int64_t offset, VfsSeekWhence whence);
    void Close(std::int32_t descriptor);
    [[nodiscard]] std::int32_t Duplicate(std::int32_t descriptor);

    // ---- directories, metadata and the sandbox (vfs_sandbox.cpp) --------
    void SetApplicationOwner(std::string_view data_root, std::uint32_t uid);
    void SetLastModified(std::string_view path, std::int64_t millis);
    void Chmod(std::string_view path, std::uint32_t mode);
    void Fchmod(std::int32_t fd, std::uint32_t mode);
    [[nodiscard]] VfsFileInfo Stat(std::string_view path) const;
    [[nodiscard]] GuestFileSystemStats StatVfs(std::string_view path) const;
    [[nodiscard]] GuestFileSystemStats StatVfs(std::int32_t descriptor) const;
    [[nodiscard]] GuestFileSystemStats VolumeStatsLocked(std::string_view path) const;
    [[nodiscard]] GuestFileSystemStats SandboxStatsLocked(std::uint32_t filesystem_type) const;
    [[nodiscard]] std::string ResolveDataPathLocked(std::string_view path) const;
    // A writable mount owns its directories: they outlive the files that were mounted in them.
    void RegisterMountDirectoriesLocked(std::string_view canonical, std::string_view spelling);
    [[nodiscard]] std::string NewPathSpellingLocked(std::string_view canonical, std::string_view path) const;
    [[nodiscard]] VfsFileInfo StatLocked(const std::string& normalized) const;
    [[nodiscard]] std::vector<VfsDirectoryEntry> ListDirectory(std::string_view path) const;
    void CreateDirectory(std::string_view path);
    void RemoveFile(std::string_view path);
    void RemoveDirectory(std::string_view path);
    void Rename(std::string_view from, std::string_view to);
    void Truncate(std::int32_t descriptor, std::uint64_t size);
    void Flush(std::int32_t descriptor);
    void FlushAll();
    void AttachSandbox(SandboxStore& store, std::span<const std::string> writable_roots);
    [[nodiscard]] bool SandboxAttached() const;
    [[nodiscard]] VfsIoStatistics IoStatistics() const;
    [[nodiscard]] std::optional<VfsSnapshot> TrySnapshot() const;
    [[nodiscard]] std::shared_ptr<const VfsResourceReservation> ReserveResourceMemory(std::uint64_t bytes,
                                                                                      bool snapshot);

    void Materialize(File& file);
    void RejectMappedMutationLocked(File& file) const;

    // Callers hold mutex_ for every helper below.
    [[nodiscard]] std::uint32_t AllocateNodeIdLocked() const;
    [[nodiscard]] bool IsDirectoryLocked(const std::string& path) const;
    [[nodiscard]] std::uint32_t OwnerLocked(std::string_view path) const;
    [[nodiscard]] VfsFileInfo FileInfoLocked(const File& file, std::uint32_t owner) const;
    [[nodiscard]] VfsFileMetadata& PathMetadataLocked(const std::string& path, const VfsFileInfo& info);
    void UpdateDirectoryMetadataLocked(std::string_view path, const VfsFileMetadata& metadata);
    std::string application_data_root_;
    std::uint32_t application_uid_{};
    std::map<std::string, VfsFileMetadata, std::less<>> directory_metadata_;
    // Identity alone never establishes directory existence; const stat/list
    // allocate here only after namespace lookup, under mutex_.
    mutable std::map<std::string, std::uint32_t, std::less<>> directory_inodes_;
    [[nodiscard]] std::uint32_t DirectoryInodeLocked(const std::string& path) const;
    // Never fails for an enumerated child: listing must survive names whose own Stat reports an error.
    [[nodiscard]] std::uint32_t EntryInodeLocked(const std::string& path) const;
    [[nodiscard]] VfsFileInfo DirectoryInfoLocked(const std::string& path) const;
    // True when the path sits under a writable namespace root, which is the
    // only place the overlay accepts writes.
    [[nodiscard]] bool IsWritableNamespaceLocked(const std::string& path) const;
    void MarkOverlayLocked(const std::string& path, File& file);
    void FlushFileLocked(File& file);
    void RequireSandboxQuotaLocked(const File& target, std::uint64_t prospective_size) const;
    // The only place a node's size or dirty flag may change: it keeps the
    // running dirty-byte aggregate consistent so quota checks stay O(1)
    // instead of rescanning every file per write.
    void SetNodeSizeDirtyLocked(File& file, std::uint64_t size, bool dirty);
    void PersistDirectoryLocked(const std::string& path);
    void PersistRemovalLocked(const std::string& path);
    [[nodiscard]] std::int32_t AllocateDescriptor() const;
    [[nodiscard]] std::shared_ptr<OpenFile> FindDescriptor(std::int32_t descriptor) const;
    // The calling thread's table (VfsThreadBinding, else table 0).
    [[nodiscard]] DescriptorTable& CurrentTableLocked() const;
    [[nodiscard]] VfsDescriptorTableId CurrentTableId() const;
    // Removes the entry and returns its open state; EBADF when absent.
    [[nodiscard]] std::shared_ptr<OpenFile> DetachDescriptorLocked(DescriptorTable& table, std::int32_t descriptor);
    // Last-close work for a detached open state: waits in-flight operations
    // and flushes dirty writable files.
    void RetireOpenFile(const std::shared_ptr<OpenFile>& open);
    void DuplicateTo(std::int32_t source, std::int32_t target, bool close_on_exec);
    void SetCloseOnExec(std::int32_t descriptor, bool close_on_exec);
    [[nodiscard]] bool CloseOnExec(std::int32_t descriptor) const;
    void CloseOnExecDescriptors();
    void InstallStreamEndpoint(std::int32_t descriptor, std::shared_ptr<const VfsStreamEndpoint> endpoint);
    [[nodiscard]] std::int32_t OpenStreamEndpoint(std::shared_ptr<const VfsStreamEndpoint> endpoint,
                                                  VfsOpenStatus status);
    [[nodiscard]] std::shared_ptr<const VfsStreamEndpoint> StreamEndpoint(std::int32_t descriptor) const;
    [[nodiscard]] VfsDescriptorTableId ForkDescriptorTable(VfsDescriptorTableId source);
    void ReleaseDescriptorTable(VfsDescriptorTableId table);
    [[nodiscard]] std::size_t ReadPipe(OpenFile& open, std::span<std::byte> destination);
    [[nodiscard]] std::size_t WritePipe(OpenFile& open, std::span<const std::byte> source);
    // Waits for a readiness change after `observed`; EINTR when the calling
    // thread's binding reports an interruption.
    void WaitPipeInterruptible(std::uint64_t observed) const;

    template <typename Visitor>
    void ForEachOpenLocked(Visitor&& visitor) const {
        for (const auto& [id, table] : descriptor_tables_) {
            static_cast<void>(id);
            for (const auto& [fd, open] : table.entries)
                visitor(fd, open);
        }
    }

    mutable std::mutex mutex_;
    // Leaf lock inside: holders never take another VFS lock.
    std::shared_ptr<PipeReadiness> pipe_readiness_{std::make_shared<PipeReadiness>()};
    // Separate from mutex_ so notification never runs under the index lock.
    mutable std::mutex lookup_observer_mutex_;
    std::shared_ptr<const VfsLookupObserver> lookup_observer_;
    // Lets successful lookups skip the observer lock while none is attached.
    std::atomic<bool> lookup_observed_{};
    void NotifyLookup(std::string_view path, bool found) const noexcept;
    std::optional<std::string> working_directory_{"/"};
    std::string working_directory_spelling_{"/"};
    bool working_directory_unlinked_{};
    std::map<std::string, std::string, std::less<>> aliases_;
    std::map<std::string, std::shared_ptr<File>, std::less<>> files_;
    // Folded path -> original spelling for explicit directories.
    std::map<std::string, std::string, std::less<>> directories_;
    std::vector<VfsMountSnapshot> mounts_;
    mutable std::map<VfsDescriptorTableId, DescriptorTable> descriptor_tables_{{kVfsApplicationDescriptorTable, {}}};
    VfsDescriptorTableId next_descriptor_table_{1U};
    // Sandbox overlay (ADR-0020). Absent until AttachSandbox.
    SandboxStore* sandbox_{};
    std::vector<std::string> writable_roots_;
    // Paths the guest deleted that the read-only base layer still provides.
    std::set<std::string, std::less<>> tombstones_;
    // Σ over dirty overlay nodes of (size - persisted_size), maintained by
    // SetNodeSizeDirtyLocked and FlushFileLocked. Sizes are bounded by the
    // sandbox quota and host memory, so a signed 64-bit delta cannot wrap.
    std::int64_t dirty_overlay_delta_{};
    std::shared_ptr<ResourceBudget> resource_budget_{std::make_shared<ResourceBudget>()};
    std::shared_ptr<HostFilePool> host_pool_;
    mutable std::uint64_t next_node_id_{1U};
    std::atomic_uint64_t flushes_{};
    std::atomic_uint64_t backing_read_bytes_{};
    std::atomic_uint64_t full_materialized_bytes_{};
};

}  // namespace ogplay::runtime
