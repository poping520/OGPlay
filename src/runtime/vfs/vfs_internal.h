#pragma once

// Internal VFS state shared by the core translation unit (vfs.cpp) and the
// sandbox overlay one (vfs_sandbox.cpp). Not installed; include order is
// private to runtime/vfs.

#include <cstdint>
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
inline constexpr std::int32_t kEnotempty = 39;

[[nodiscard]] std::string NormalizeAbsolutePath(std::string_view path,
                                                bool allow_root);
[[nodiscard]] std::string NormalizePath(std::string_view path);
[[nodiscard]] std::string ResolvePath(
    std::string_view path,
    const std::optional<std::string>& working_directory);
[[nodiscard]] std::vector<std::byte> ReadHostFile(
    const std::filesystem::path& path);

struct File final {
    std::vector<std::byte> contents;
    std::uint64_t size{};
    VfsReadOnlyLoader read_all;
    bool writable{};
    VfsSource source{VfsSource::runtime};
    // Sandbox bookkeeping: set once the node belongs to the overlay, so a
    // rename carries the persistence identity with the node.
    bool dirty{};
    std::string overlay_path;
};

// A descriptor opened on a directory: the children are snapshotted so
// getdents64 paging stays stable across calls.
struct OpenDirectoryState final {
    std::vector<VfsDirectoryEntry> entries;
    std::size_t cursor{};
};

struct OpenFile final {
    std::shared_ptr<File> file;
    std::uint64_t offset{};
    bool readable{};
    bool writable{};
    std::shared_ptr<OpenDirectoryState> directory;
};

class VirtualFileSystem::Impl final {
public:
    // ---- mounts and files (vfs.cpp) -------------------------------------
    void PutFile(std::string_view path, std::span<const std::byte> contents,
                 bool writable);
    void Mount(VfsSource source, std::string_view root,
               std::span<const VfsMountEntry> entries);
    void MountLazy(VfsSource source, std::string_view root,
                   std::span<const VfsLazyMountEntry> entries, bool writable);
    void MountHostDirectory(std::string_view root,
                            const std::filesystem::path& directory);
    [[nodiscard]] std::optional<std::filesystem::path> HostPathFor(
        std::string_view path) const;
    void SetWorkingDirectory(std::string_view path);
    [[nodiscard]] std::optional<std::string> WorkingDirectory() const;
    [[nodiscard]] std::int32_t Open(std::string_view path,
                                    VfsOpenOptions options);
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

    static void Materialize(File& file);

    // Callers hold mutex_ for every helper below.
    [[nodiscard]] bool IsDirectoryLocked(const std::string& path) const;
    // True when the path sits under a writable namespace root, which is the
    // only place the overlay accepts writes.
    [[nodiscard]] bool IsWritableNamespaceLocked(const std::string& path) const;
    void MarkOverlayLocked(const std::string& path, File& file);
    void FlushFileLocked(File& file);
    void PersistDirectoryLocked(const std::string& path);
    void PersistRemovalLocked(const std::string& path, bool had_base_layer);
    [[nodiscard]] std::int32_t AllocateDescriptor() const;
    [[nodiscard]] OpenFile& FindDescriptor(std::int32_t descriptor);

    mutable std::mutex mutex_;
    std::optional<std::string> working_directory_;
    std::map<std::string, std::shared_ptr<File>, std::less<>> files_;
    // Directories created explicitly; implicit ones come from files_.
    std::set<std::string, std::less<>> directories_;
    std::map<std::int32_t, OpenFile> descriptors_;
    // Guest path -> backing host file for host-directory mounts, so media
    // decoders can open the real file directly.
    std::map<std::string, std::filesystem::path, std::less<>> host_backed_;

    // Sandbox overlay (ADR-0020). Absent until AttachSandbox.
    SandboxStore* sandbox_{};
    std::vector<std::string> writable_roots_;
    // Paths the guest deleted that the read-only base layer still provides.
    std::set<std::string, std::less<>> tombstones_;
};

}  // namespace ogplay::runtime
