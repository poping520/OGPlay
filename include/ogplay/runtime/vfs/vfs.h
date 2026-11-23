#pragma once

#include "ogplay/runtime/common/guest_filesystem_stats.h"

// Android-path virtual filesystem core shared by syscall and framework HLE.

#include <chrono>
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
struct VfsGeneratedFileState;
struct VfsCharacterDeviceState;
using VfsCharacterReader = std::function<std::size_t(std::span<std::byte>)>;

// Retirement waits for in-flight reads; retained descriptors then fail with EIO.
class VfsCharacterDeviceRegistration final {
public:
    ~VfsCharacterDeviceRegistration();
    VfsCharacterDeviceRegistration(const VfsCharacterDeviceRegistration&) = delete;
    VfsCharacterDeviceRegistration& operator=(const VfsCharacterDeviceRegistration&) = delete;

private:
    friend class VirtualFileSystem;

    explicit VfsCharacterDeviceRegistration(std::shared_ptr<VfsCharacterDeviceState> state)
        : state_(std::move(state)) {}

    std::shared_ptr<VfsCharacterDeviceState> state_;
};

// Revokes the provider synchronously on destruction. Open snapshots remain valid.
class VfsGeneratedFileRegistration final {
public:
    ~VfsGeneratedFileRegistration();
    VfsGeneratedFileRegistration(const VfsGeneratedFileRegistration&) = delete;
    VfsGeneratedFileRegistration& operator=(const VfsGeneratedFileRegistration&) = delete;

private:
    friend class VirtualFileSystem;

    explicit VfsGeneratedFileRegistration(std::shared_ptr<VfsGeneratedFileState> state) : state_(std::move(state)) {}

    std::shared_ptr<VfsGeneratedFileState> state_;
};

class VfsReadLease {
public:
    virtual ~VfsReadLease() = default;
    [[nodiscard]] virtual std::uint64_t Size() const noexcept = 0;
    [[nodiscard]] virtual std::size_t ReadAt(std::uint64_t offset, std::span<std::byte> destination,
                                             std::stop_token stop = {}) const = 0;
};

struct VfsMappingSource final {
    std::shared_ptr<const VfsReadLease> lease;
    bool readable{}, writable{};
};

struct VfsOpenOptions final {
    bool read{};
    bool write{};
    bool create{};
    bool truncate{};
    bool directory{};
    bool non_blocking{};
    // Open-description status shared by Duplicate; writes select EOF atomically.
    bool append{};
    bool large_file{};
    bool no_follow{};
    bool exclusive{};
    // Host callers that must not materialize a whole backing use this with read-only open.
    bool require_positioned_read{};
};

// Immutable open-description flags, excluding creation and descriptor flags.
struct VfsOpenStatus final {
    bool read{};
    bool write{};
    bool append{};
    bool non_blocking{};
    bool directory{};
    bool large_file{};
    bool no_follow{};
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
        if constexpr (std::is_invocable_r_v<std::size_t, Loader&, std::uint64_t, std::span<std::byte>,
                                            std::stop_token>) {
            loader_ = std::move(loader);
        } else {
            static_assert(std::is_invocable_r_v<std::size_t, Loader&, std::uint64_t, std::span<std::byte>>);
            loader_ = [loader = std::move(loader)](const std::uint64_t offset, const std::span<std::byte> destination,
                                                   const std::stop_token stop) mutable {
                if (stop.stop_requested()) return std::size_t{};
                return loader(offset, destination);
            };
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(loader_); }

    [[nodiscard]] std::size_t operator()(const std::uint64_t offset, const std::span<std::byte> destination,
                                         const std::stop_token stop = {}) const {
        return loader_(offset, destination, stop);
    }

private:
    std::function<std::size_t(std::uint64_t, std::span<std::byte>, std::stop_token)> loader_;
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

struct VfsFileMetadata final {
    std::optional<std::uint32_t> permissions;
    std::int64_t modified_millis{};
};

struct VfsFileInfo final {
    std::uint64_t size{};
    bool writable{};
    VfsSource source{VfsSource::runtime};
    bool is_directory{};
    std::uint64_t generation{};
    bool is_character_device{};
    std::uint64_t device_number{};
    std::uint32_t uid{}, gid{};
    VfsFileMetadata metadata{};

    [[nodiscard]] std::uint32_t Mode() const noexcept {
        const auto type = is_directory ? 0040000U : is_character_device ? 0020000U : 0100000U;
        const auto defaults = is_directory ? (writable ? 0755U : 0555U) : (writable ? 0644U : 0444U);
        return type | metadata.permissions.value_or(defaults);
    }
};

struct VfsDirectoryEntry final {
    std::string name;
    bool is_directory{};
    bool is_character_device{};

    bool operator==(const VfsDirectoryEntry&) const = default;
};

// Descriptor table of one (virtual) process. Table 0 is the guest
// application process and always exists.
using VfsDescriptorTableId = std::uint32_t;
inline constexpr VfsDescriptorTableId kVfsApplicationDescriptorTable = 0;
// Linux pipe capacity and the size up to which pipe writes are atomic.
inline constexpr std::size_t kVfsPipeCapacity = 64U * 1024U;
inline constexpr std::size_t kVfsPipeAtomicWrite = 4096U;

// A descriptor that is a byte stream owned by an upper layer (stdin/stdout/
// stderr, Android log devices). Reads return EOF; writes go to `write`.
struct VfsStreamEndpoint final {
    std::string name;
    // Opaque to the VFS; the installing layer identifies its endpoints by it.
    std::uint32_t kind{};
    bool readable{};
    bool writable{};
    // Writes made through the VFS (for example by built-in commands). Empty
    // makes such writes fail with EBADF.
    std::function<void(std::span<const std::byte>)> write;
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
    std::uint64_t host_file_handles{};
    std::uint64_t host_file_handle_high_water{};
    std::uint64_t host_file_handle_evictions{};
};

struct VfsDescriptorSnapshot final {
    std::int32_t fd{};
    std::optional<std::uint64_t> node_id, offset;
    bool readable{}, writable{}, busy{};
};

struct VfsMountSnapshot final {
    std::string root;
    VfsSource source{};
};

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
    // Includes idle cached handles and active FD/lease pins, shared by all
    // host mounts in this VFS. Exhaustion fails explicitly with EMFILE.
    std::uint32_t host_file_handle_budget{128U};
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

    [[nodiscard]] std::int32_t ErrorNumber() const noexcept { return error_number_; }

private:
    std::int32_t error_number_{};
};

// Diagnostics only (ADR-0146): receives the canonical guest path of a read
// lookup (Stat, non-creating Open, OpenDirectory, ListDirectory) and whether
// it succeeded (found) or failed with ENOENT; other errors are not reported.
// Runs on the calling guest thread outside VFS locks, must not re-enter the
// VFS, and cannot change the guest-visible result.
using VfsLookupObserver = std::function<void(std::string_view path, bool found)>;

class VirtualFileSystem final {
public:
    explicit VirtualFileSystem(VfsConfig config = {});
    ~VirtualFileSystem();
    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

    // Provider runs outside VFS locks, once per open; stat size is zero.
    // Caller bounds allocation to maximum_bytes; the VFS reserves that budget first.
    [[nodiscard]] std::unique_ptr<VfsGeneratedFileRegistration>
    RegisterGeneratedReadOnly(std::string_view path, std::uint64_t maximum_bytes, VfsReadOnlyLoader provider);
    // Reader runs per non-empty Read outside global/node locks. No EOF is
    // inferred from st_size (zero); positioned IO and leases are unsupported.
    [[nodiscard]] std::unique_ptr<VfsCharacterDeviceRegistration>
    RegisterReadOnlyCharacterDevice(std::string_view path, std::uint64_t device_number, VfsCharacterReader reader);
    void PutFile(std::string_view path, std::span<const std::byte> contents, bool writable);
    // Read-only runtime file whose bytes stay with the caller; reads, leases
    // and mappings go through read_at, so the VFS keeps no copy.
    void PutReadOnlyFile(std::string_view path, std::uint64_t size, VfsReadAtLoader read_at);
    void Mount(VfsSource source, std::string_view root, std::span<const VfsMountEntry> entries);
    void MountLazyReadOnly(VfsSource source, std::string_view root, std::span<const VfsLazyMountEntry> entries);
    void MountHostDirectory(std::string_view root, const std::filesystem::path& directory);
    void MountHostFile(VfsSource source, std::string_view root, const std::filesystem::path& file);
    // Canonicalizes a path prefix onto an already mounted namespace. Both
    // spellings then address the same nodes and overlay.
    void AddPathAlias(std::string_view alias, std::string_view target);
    // An empty function detaches the observer.
    void SetLookupObserver(VfsLookupObserver observer);
    void SetWorkingDirectory(std::string_view path);
    void SetWorkingDirectory(std::int32_t descriptor);
    [[nodiscard]] std::optional<std::string> WorkingDirectory() const;
    // Returns the normalized node identity used internally by all VFS file
    // operations, including path aliases.
    [[nodiscard]] std::string CanonicalPath(std::string_view path) const;
    // Returns an existing file node's published spelling after normal path
    // resolution. Missing files throw ENOENT; directories throw EISDIR.
    [[nodiscard]] std::string FileSpelling(std::string_view path) const;
    void SetApplicationOwner(std::string_view data_root, std::uint32_t uid);
    void SetLastModified(std::string_view path, std::int64_t millis);
    void Chmod(std::string_view path, std::uint32_t mode);
    void Fchmod(std::int32_t fd, std::uint32_t mode);
    [[nodiscard]] VfsFileInfo Stat(std::string_view path) const;
    [[nodiscard]] GuestFileSystemStats StatVfs(std::string_view path) const;
    [[nodiscard]] GuestFileSystemStats StatVfs(std::int32_t descriptor) const;
    // Immediate children (sorted, deduplicated) of a directory path; empty
    // when nothing is below it. Directories exist both implicitly, through
    // the files mounted beneath them, and explicitly through CreateDirectory.
    [[nodiscard]] std::vector<VfsDirectoryEntry> ListDirectory(std::string_view path) const;

    // Directory and metadata operations (ADR-0020). Pure in-memory
    // semantics; a sandbox attached later persists them at flush points.
    void CreateDirectory(std::string_view path);  // parent must exist
    void RemoveFile(std::string_view path);       // unlink
    void RemoveDirectory(std::string_view path);  // rmdir, -ENOTEMPTY
    void Rename(std::string_view from, std::string_view to);
    [[nodiscard]] std::int32_t Open(std::string_view path, VfsOpenOptions options);
    // Directory descriptor for getdents64: the child list is snapshotted at
    // open time and consumed by ReadDirectory, so paging is stable even if
    // the guest creates files while walking.
    [[nodiscard]] std::int32_t OpenDirectory(std::string_view path);
    [[nodiscard]] std::vector<VfsDirectoryEntry> ReadDirectory(std::int32_t descriptor, std::size_t maximum);
    [[nodiscard]] VfsFileInfo DescriptorInfo(std::int32_t descriptor) const;
    [[nodiscard]] VfsOpenStatus DescriptorStatus(std::int32_t descriptor) const;
    // Append status is immutable for the lifetime of an open description.
    [[nodiscard]] bool IsAppend(std::int32_t descriptor) const;
    [[nodiscard]] VfsPipeDescriptors CreatePipe(bool non_blocking = false);
    // ALOOPER-style bits: read=1, write=2, error=4, hangup=8, invalid=16.
    // nullopt means the descriptor type is outside the pipe polling contract.
    [[nodiscard]] std::optional<std::uint32_t> PipePollEvents(std::int32_t descriptor) const;
    // Bytes buffered in a pipe; nullopt when the descriptor is not a pipe.
    [[nodiscard]] std::optional<std::size_t> PipeBuffered(std::int32_t descriptor) const;
    // Counter bumped after a pipe write or a pipe descriptor close. Read it before
    // PipePollEvents, then WaitPipeReadiness with that value so no change is lost.
    [[nodiscard]] std::uint64_t PipeReadinessGeneration() const;
    // Returns once the counter differs from `observed` or `timeout` elapses.
    void WaitPipeReadiness(std::uint64_t observed, std::chrono::nanoseconds timeout) const;
    [[nodiscard]] std::size_t Read(std::int32_t descriptor, std::span<std::byte> destination);
    [[nodiscard]] std::size_t ReadAt(std::int32_t descriptor, std::uint64_t offset, std::span<std::byte> destination);
    [[nodiscard]] std::size_t Write(std::int32_t descriptor, std::span<const std::byte> source);
    [[nodiscard]] std::size_t WriteAt(std::int32_t descriptor, std::uint64_t offset, std::span<const std::byte> source);
    // Pins a regular node and its bytes against VFS mutations (EBUSY). Diagnostic
    // runs under the metadata lock and must not reenter VFS. Token outlives FD.
    [[nodiscard]] VfsMappingSource CaptureMappingSource(std::int32_t descriptor, std::function<void()> rejected);
    [[nodiscard]] std::shared_ptr<const VfsReadLease>
    CaptureReadLease(std::int32_t descriptor, std::uint64_t offset,
                     std::uint64_t length = std::numeric_limits<std::uint64_t>::max());
    [[nodiscard]] std::uint64_t Seek(std::int32_t descriptor, std::int64_t offset, VfsSeekWhence whence);
    // Attaches the per-title overlay. Writes inside writable_roots then
    // persist at the flush points; everything else keeps its current
    // read-only base-layer behaviour.
    void AttachSandbox(SandboxStore& store, std::span<const std::string> writable_roots);
    [[nodiscard]] bool SandboxAttached() const;
    [[nodiscard]] VfsIoStatistics IoStatistics() const;
    [[nodiscard]] std::optional<VfsSnapshot> TrySnapshot() const;
    [[nodiscard]] std::optional<std::uint64_t> TryDescriptorNode(std::int32_t descriptor) const;
    // Reserves from the same aggregate memory budget used by writable lease
    // snapshots. Callers must reserve before allocating and retain the token
    // for as long as the allocation is reachable.
    [[nodiscard]] std::shared_ptr<const VfsResourceReservation> ReserveResourceMemory(std::uint64_t bytes);

    void Truncate(std::int32_t descriptor, std::uint64_t size);
    // fsync/fdatasync join here; without an attached sandbox both only
    // check the descriptor, which keeps the call honest rather than absent.
    void Flush(std::int32_t descriptor);
    void FlushAll();
    void Close(std::int32_t descriptor);
    // Shares the open state and its operation lock, as Linux dup does.
    [[nodiscard]] std::int32_t Duplicate(std::int32_t descriptor);
    // dup2/dup3: closes `target` if open, then shares `source`'s open state
    // under that exact number. Equal numbers only validate `source`.
    void DuplicateTo(std::int32_t source, std::int32_t target, bool close_on_exec = false);
    void SetCloseOnExec(std::int32_t descriptor, bool close_on_exec);
    [[nodiscard]] bool CloseOnExec(std::int32_t descriptor) const;
    // Closes every close-on-exec descriptor of the calling thread's table.
    void CloseOnExecDescriptors();

    // Stream endpoints share the table and dup/close semantics of files.
    void InstallStreamEndpoint(std::int32_t descriptor, std::shared_ptr<const VfsStreamEndpoint> endpoint);
    [[nodiscard]] std::int32_t OpenStreamEndpoint(std::shared_ptr<const VfsStreamEndpoint> endpoint,
                                                  VfsOpenStatus status);
    // Null when the descriptor is absent or not an endpoint.
    [[nodiscard]] std::shared_ptr<const VfsStreamEndpoint> StreamEndpoint(std::int32_t descriptor) const;

    // Per-process descriptor tables. A fork shares every open state, as
    // Linux fork without CLONE_FILES does, and keeps close-on-exec flags.
    [[nodiscard]] VfsDescriptorTableId ForkDescriptorTable(VfsDescriptorTableId source);
    // Closes all descriptors of a forked table. Table 0 cannot be released.
    void ReleaseDescriptorTable(VfsDescriptorTableId table);
    // The table descriptor operations of the calling thread act on.
    [[nodiscard]] VfsDescriptorTableId CurrentDescriptorTable() const;

private:
    friend class VfsThreadBinding;
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Binds the calling thread's descriptor operations on `vfs` to `table`
// until destruction; bindings nest. `interrupted` is polled by blocking pipe
// operations, which then fail with EINTR. Unbound threads use table 0 and
// block uninterruptibly.
class VfsThreadBinding final {
public:
    VfsThreadBinding(const VirtualFileSystem& vfs, VfsDescriptorTableId table, std::function<bool()> interrupted = {});
    ~VfsThreadBinding();
    VfsThreadBinding(const VfsThreadBinding&) = delete;
    VfsThreadBinding& operator=(const VfsThreadBinding&) = delete;

private:
    const void* owner_{};
    VfsDescriptorTableId table_{};
    std::function<bool()> interrupted_;
};

}  // namespace ogplay::runtime
