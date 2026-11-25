// Directory/metadata operations and the per-title sandbox overlay
// (ADR-0020, docs/design/storage-security.md). Resolution order is
// overlay file, then overlay tombstone (absent), then the read-only base
// layer; writes inside the writable namespace land in the overlay and reach
// the host at the enumerated flush points.

#include <algorithm>
#include <limits>
#include <utility>

#include "vfs_internal.h"

namespace ogplay::runtime {

namespace {

constexpr std::uint64_t kFilesystemBlockSize = 4096;
constexpr std::uint32_t kExt4Magic = 0xEF53;
constexpr std::uint64_t kReadOnlyFilesystem = 1;

// Linux makedev(0, minor), with a distinct nonzero minor per guest volume.
enum class Volume : std::uint32_t { rootfs = 1, data, sdcard, system, proc, dev, pipefs };

[[nodiscard]] Volume PathVolume(std::string_view path) {
    const auto under = [path](std::string_view root) {
        return path == root || (path.starts_with(root) && path.size() > root.size() && path[root.size()] == '/');
    };
    if (under("/data")) return Volume::data;
    if (under("/sdcard")) return Volume::sdcard;
    if (under("/system")) return Volume::system;
    if (under("/proc")) return Volume::proc;
    if (under("/dev")) return Volume::dev;
    return Volume::rootfs;
}

[[nodiscard]] constexpr std::uint64_t FilesystemId(std::string_view name) {
    std::uint64_t id = UINT64_C(14695981039346656037);
    for (const auto c : name) {
        id ^= static_cast<unsigned char>(c);
        id *= UINT64_C(1099511628211);
    }
    id &= static_cast<std::uint64_t>(INT64_MAX);
    return id == 0 ? 1 : id;
}

[[nodiscard]] constexpr GuestFileSystemStats SyntheticVolumeStats(std::string_view name, std::uint32_t type,
                                                                  std::uint64_t flags = 0) {
    return {{kFilesystemBlockSize, kFilesystemBlockSize, 0, 0, 0, 0, 0, 0, FilesystemId(name), flags, 255}, type};
}

}  // namespace

std::uint32_t VirtualFileSystem::Impl::AllocateNodeIdLocked() const {
    if (next_node_id_ > UINT32_MAX) throw VfsError(75, "VFS guest inode space exhausted");
    return static_cast<std::uint32_t>(next_node_id_++);
}

void VirtualFileSystem::Impl::SetApplicationOwner(std::string_view data_root, std::uint32_t uid) {
    std::scoped_lock lock(mutex_);
    application_data_root_ = NormalizePath(data_root);
    application_uid_ = uid;
}

std::uint32_t VirtualFileSystem::Impl::OwnerLocked(std::string_view path) const {
    const auto under = [path](std::string_view root) {
        return path == root || (path.starts_with(root) && path.size() > root.size() && path[root.size()] == '/');
    };
    if (!application_data_root_.empty() && under(application_data_root_)) return application_uid_;
    if (under("/data")) return 1000;                         // AID_SYSTEM
    if (under("/sdcard") || under("/storage")) return 1023;  // AID_MEDIA_RW
    return 0;
}

VfsFileInfo VirtualFileSystem::Impl::FileInfoLocked(const File& file, std::uint32_t owner) const {
    return {file.size,
            file.writable,
            file.source,
            false,
            file.generation,
            static_cast<bool>(file.character_device),
            file.character_device ? file.character_device->device_number : 0,
            owner,
            owner,
            file.metadata,
            static_cast<std::uint32_t>(PathVolume(file.canonical_path)),
            file.inode};
}

VfsFileMetadata& VirtualFileSystem::Impl::PathMetadataLocked(const std::string& path, const VfsFileInfo& info) {
    if (IsSystemPartitionPath(path)) throw VfsError(kErofs, "read-only VFS system partition");
    if (!info.writable || (sandbox_ && !IsWritableNamespaceLocked(path)))
        throw VfsError(1, "VFS metadata is not writable by the application");
    return info.is_directory ? directory_metadata_[path] : files_.at(path)->metadata;
}

void VirtualFileSystem::Impl::UpdateDirectoryMetadataLocked(std::string_view path, const VfsFileMetadata& metadata) {
    ForEachOpenLocked([&](std::int32_t, const std::shared_ptr<OpenFile>& open) {
        if (open->directory && !open->directory->unlinked && open->directory->path == path)
            open->directory->info.metadata = metadata;
    });
}

void VirtualFileSystem::Impl::SetLastModified(std::string_view path, std::int64_t millis) {
    std::scoped_lock lock(mutex_);
    const auto normalized = ResolveDataPathLocked(path);
    const auto info = StatLocked(normalized);
    if (millis < 0 || millis / 1000 > INT32_MAX) throw VfsError(22, "timestamp is outside API19 time_t");
    auto& metadata = PathMetadataLocked(normalized, info);
    metadata.modified_millis = millis;
    if (info.is_directory) UpdateDirectoryMetadataLocked(normalized, metadata);
}

void VirtualFileSystem::Impl::Chmod(std::string_view path, std::uint32_t mode) {
    std::scoped_lock lock(mutex_);
    const auto normalized = ResolveDataPathLocked(path);
    const auto info = StatLocked(normalized);
    auto& metadata = PathMetadataLocked(normalized, info);
    metadata.permissions = mode & 07777U;
    if (info.is_directory) UpdateDirectoryMetadataLocked(normalized, metadata);
}

void VirtualFileSystem::Impl::Fchmod(std::int32_t fd, std::uint32_t mode) {
    std::scoped_lock lock(mutex_);
    const auto open = FindDescriptor(fd);
    if (open->directory ? open->directory->system_partition : open->system_partition)
        throw VfsError(kErofs, "read-only VFS system partition");
    if (open->directory && !open->directory->unlinked) {
        const auto& path = open->directory->path;
        auto& metadata = PathMetadataLocked(path, open->directory->info);
        metadata.permissions = mode & 07777U;
        UpdateDirectoryMetadataLocked(path, metadata);
    } else if (open->directory && open->directory->info.writable) {
        open->directory->info.metadata.permissions = mode & 07777U;
    } else if (open->file && open->file->writable) {
        open->file->metadata.permissions = mode & 07777U;
    } else {
        throw VfsError(1, "VFS descriptor metadata is not writable by the application");
    }
}

VfsFileInfo VirtualFileSystem::Impl::Stat(const std::string_view path) const {
    std::scoped_lock lock(mutex_);
    const auto normalized = ResolveDataPathLocked(path);
    return StatLocked(normalized);
}

VfsFileInfo VirtualFileSystem::Impl::StatLocked(const std::string& normalized) const {
    if (tombstones_.contains(normalized)) {
        throw VfsError(kEnoent, "VFS file was deleted");
    }
    const auto found = files_.find(normalized);
    if (found != files_.end()) {
        if (found->second->generated && !found->second->generated->active)
            throw VfsError(kEnoent, "VFS generated file provider retired");
        if (found->second->character_device && !found->second->character_device->active)
            throw VfsError(kEnoent, "VFS character device retired");
        return FileInfoLocked(*found->second, OwnerLocked(normalized));
    }
    if (IsDirectoryLocked(normalized)) {
        return DirectoryInfoLocked(normalized);
    }
    throw VfsError(kEnoent, "VFS file not found");
}

GuestFileSystemStats VirtualFileSystem::Impl::StatVfs(std::string_view path) const {
    std::scoped_lock lock(mutex_);
    const auto normalized = ResolveDataPathLocked(path);
    static_cast<void>(StatLocked(normalized));
    return VolumeStatsLocked(normalized);
}

GuestFileSystemStats VirtualFileSystem::Impl::StatVfs(std::int32_t descriptor) const {
    std::scoped_lock lock(mutex_);
    const auto open = FindDescriptor(descriptor);
    if (open->pipe) return SyntheticVolumeStats("pipefs", 0x50495045);
    return VolumeStatsLocked(open->directory ? open->directory->path : open->file->canonical_path);
}

GuestFileSystemStats VirtualFileSystem::Impl::VolumeStatsLocked(std::string_view path) const {
    switch (PathVolume(path)) {
    case Volume::data: return SandboxStatsLocked(kExt4Magic);
    case Volume::sdcard: return SandboxStatsLocked(0x65735546);
    case Volume::proc: return SyntheticVolumeStats("proc", 0x9FA0);
    case Volume::dev: return SyntheticVolumeStats("dev", 0x01021994);
    case Volume::system: break;
    default: return SyntheticVolumeStats("rootfs", 0x858458F6, kReadOnlyFilesystem);
    }

    auto stats = SyntheticVolumeStats("system", kExt4Magic, kReadOnlyFilesystem);
    std::uint64_t bytes = 0;
    std::uint64_t nodes = 0;
    for (auto it = files_.lower_bound("/system"); it != files_.end() && it->first.starts_with("/system"); ++it) {
        const auto& [name, file] = *it;
        if ((name != "/system" && !name.starts_with("/system/")) || tombstones_.contains(name) ||
            (file->generated && !file->generated->active) ||
            (file->character_device && !file->character_device->active))
            continue;
        if (file->size > UINT64_MAX - bytes) throw VfsError(75, "system volume size exceeds statistics range");
        bytes += file->size;
        ++nodes;
    }
    for (auto it = directories_.lower_bound("/system"); it != directories_.end() && it->first.starts_with("/system");
         ++it) {
        if (IsSystemPartitionPath(it->first) && !tombstones_.contains(it->first)) ++nodes;
    }
    stats.values[2] = bytes / kFilesystemBlockSize + (bytes % kFilesystemBlockSize != 0);
    stats.values[5] = nodes;
    return stats;
}

GuestFileSystemStats VirtualFileSystem::Impl::SandboxStatsLocked(std::uint32_t filesystem_type) const {
    if (!sandbox_) throw VfsError(95, "filesystem statistics require the attached sandbox volume");
    const auto quota = sandbox_->QuotaBytes();
    if (quota > static_cast<std::uint64_t>(INT64_MAX) || sandbox_->UsedBytes() > quota)
        throw VfsError(75, "sandbox quota exceeds supported filesystem statistics range");
    const auto persisted = static_cast<std::int64_t>(sandbox_->UsedBytes());
    if (dirty_overlay_delta_ > 0 && persisted > INT64_MAX - dirty_overlay_delta_)
        throw VfsError(75, "sandbox dirty quota exceeds statistics range");
    const auto used = persisted + dirty_overlay_delta_;
    if (used < 0 || static_cast<std::uint64_t>(used) > quota)
        throw VfsError(5, "sandbox dirty quota accounting is inconsistent");
    // Persisted metadata and pending new overlay files share the same entry
    // budget. Immutable base entries and tombstones do not consume it.
    std::set<std::string, std::less<>> entries;
    for (const auto& entry : sandbox_->Entries())
        if (!entry.is_tombstone) entries.insert(NormalizePath(entry.path));
    for (const auto& [_, file] : files_)
        if (file->dirty && !file->overlay_path.empty()) entries.insert(file->overlay_path);
    const auto entry_quota = sandbox_->EntryQuota();
    const auto free_entries = entry_quota > entries.size() ? entry_quota - entries.size() : 0;
    const auto fsid = FilesystemId(sandbox_->InstallationId() + "/" + sandbox_->Package());
    constexpr auto block = kFilesystemBlockSize;
    const auto free = (quota - static_cast<std::uint64_t>(used)) / block;
    // Explicit virtual quota volume: no reserved blocks or mount-option
    // flags. namemax is the existing encoded backing-name upper bound;
    // escaping/tombstone overhead can reject a guest name sooner.
    return {{block, block, quota / block, free, free, entry_quota, free_entries, free_entries, fsid, 0,
             SandboxStore::MaximumEncodedNameBytes()},
            filesystem_type};
}

std::vector<VfsDirectoryEntry> VirtualFileSystem::Impl::ListDirectory(const std::string_view path) const {
    std::scoped_lock lock(mutex_);
    auto prefix = ResolveDataPathLocked(path);
    if (prefix.empty() || prefix.back() != '/') prefix.push_back('/');
    // Order and deduplication use the folded keys, while names exposed to the guest keep their spelling.
    std::map<std::string, VfsDirectoryEntry, std::less<>> children;
    const auto collect = [&](const std::string& key, const std::string_view spelling, const bool leaf_is_directory,
                             const bool character_device) {
        if (!key.starts_with(prefix)) return;
        const auto remainder = std::string_view(key).substr(prefix.size());
        if (remainder.empty()) return;
        const auto slash = remainder.find('/');
        const bool is_leaf = slash == std::string_view::npos;
        const bool is_directory = !is_leaf || leaf_is_directory;
        if (!is_directory && tombstones_.contains(key)) return;
        const auto name = remainder.substr(0, slash);
        auto& entry = children[std::string(name)];
        if (entry.name.empty() || (is_directory && !entry.is_directory) || (is_leaf && leaf_is_directory)) {
            entry = {std::string(spelling.substr(prefix.size(), name.size())), is_directory,
                     is_leaf && character_device};
        }
    };
    for (auto it = files_.lower_bound(prefix); it != files_.end() && it->first.starts_with(prefix); ++it) {
        if ((!it->second->generated || it->second->generated->active) &&
            (!it->second->character_device || it->second->character_device->active)) {
            collect(it->first, it->second->spelling, false, it->second->character_device != nullptr);
        }
    }
    for (auto it = directories_.lower_bound(prefix); it != directories_.end() && it->first.starts_with(prefix); ++it) {
        collect(it->first, it->second, true, false);
    }
    std::vector<VfsDirectoryEntry> entries;
    entries.reserve(children.size());
    for (auto& child : children) {
        child.second.inode = EntryInodeLocked(prefix + child.first);
        entries.push_back(std::move(child.second));
    }
    return entries;
}

bool VirtualFileSystem::Impl::IsDirectoryLocked(const std::string& path) const {
    if (path == "/") return true;
    if (directories_.contains(path)) return true;
    auto prefix = path;
    prefix.push_back('/');
    for (auto file = files_.lower_bound(prefix); file != files_.end() && file->first.starts_with(prefix); ++file) {
        if ((!file->second->generated || file->second->generated->active) &&
            (!file->second->character_device || file->second->character_device->active))
            return true;
    }
    for (const auto& [alias, target] : aliases_) {
        static_cast<void>(target);
        if (alias == path || alias.starts_with(prefix)) return true;
    }
    const auto directory = directories_.lower_bound(prefix);
    return directory != directories_.end() && directory->first.starts_with(prefix);
}

std::uint32_t VirtualFileSystem::Impl::DirectoryInodeLocked(const std::string& path) const {
    auto inode = directory_inodes_.find(path);
    if (inode == directory_inodes_.end()) inode = directory_inodes_.emplace(path, AllocateNodeIdLocked()).first;
    return inode->second;
}

std::uint32_t VirtualFileSystem::Impl::EntryInodeLocked(const std::string& path) const {
    // A child name can itself be an alias; identity follows the canonical
    // lookup used by stat. The enumerated path is the fallback for an alias
    // whose target is gone.
    const auto canonical = ResolveDataPathLocked(path);
    for (const auto& candidate : {canonical, path}) {
        const auto found = files_.find(candidate);
        if (found != files_.end() && (!found->second->generated || found->second->generated->active) &&
            (!found->second->character_device || found->second->character_device->active))
            return found->second->inode;
        if (IsDirectoryLocked(candidate)) return DirectoryInodeLocked(candidate);
    }
    return DirectoryInodeLocked(path);
}

VfsFileInfo VirtualFileSystem::Impl::DirectoryInfoLocked(const std::string& path) const {
    const bool writable = !IsSystemPartitionPath(path) && (sandbox_ == nullptr || IsWritableNamespaceLocked(path));
    VfsSource source = VfsSource::runtime;
    if (!writable) {
        auto prefix = path;
        if (prefix != "/") prefix.push_back('/');
        const auto child = files_.lower_bound(prefix);
        if (child != files_.end() && child->first.starts_with(prefix)) {
            source = child->second->source;
        }
    }
    const auto owner = OwnerLocked(path);
    const auto metadata = directory_metadata_.find(path);
    return {0,
            writable,
            source,
            true,
            0,
            false,
            0,
            owner,
            owner,
            metadata != directory_metadata_.end() ? metadata->second : VfsFileMetadata{},
            static_cast<std::uint32_t>(PathVolume(path)),
            DirectoryInodeLocked(path)};
}

bool VirtualFileSystem::Impl::IsWritableNamespaceLocked(const std::string& path) const {
    for (const auto& root : writable_roots_) {
        if (path == root) return true;
        if (path.starts_with(root) && path.size() > root.size() && path[root.size()] == '/') {
            return true;
        }
    }
    return false;
}

void VirtualFileSystem::Impl::MarkOverlayLocked(const std::string& path, File& file) {
    if (sandbox_ == nullptr || !IsWritableNamespaceLocked(path)) return;
    file.overlay_path = path;
}

namespace {

// A dirty overlay node holds (size - persisted_size) bytes that the store
// has not seen yet; everything else contributes nothing.
[[nodiscard]] std::int64_t DirtyContribution(const File& file) {
    if (!file.dirty || file.overlay_path.empty()) return 0;
    return static_cast<std::int64_t>(file.size) - static_cast<std::int64_t>(file.persisted_size);
}

}  // namespace

void VirtualFileSystem::Impl::SetNodeSizeDirtyLocked(File& file, const std::uint64_t size, const bool dirty) {
    dirty_overlay_delta_ -= DirtyContribution(file);
    file.size = size;
    file.dirty = dirty;
    dirty_overlay_delta_ += DirtyContribution(file);
}

void VirtualFileSystem::Impl::FlushFileLocked(File& file) {
    if (sandbox_ == nullptr || !file.dirty || file.overlay_path.empty()) {
        return;
    }
    Materialize(file);
    sandbox_->WriteFileAtomic(file.overlay_path, file.contents);
    tombstones_.erase(file.overlay_path);
    dirty_overlay_delta_ -= DirtyContribution(file);
    file.persisted_size = file.size;
    file.dirty = false;
}

void VirtualFileSystem::Impl::RequireSandboxQuotaLocked(const File& target,
                                                        const std::uint64_t prospective_size) const {
    if (sandbox_ == nullptr || target.overlay_path.empty()) return;
    // Store bytes plus every dirty node's unpersisted delta, with the
    // target's contribution replaced by the prospective size. O(1): the
    // aggregate is maintained at each size/dirty mutation.
    const auto projected = static_cast<std::int64_t>(sandbox_->UsedBytes()) + dirty_overlay_delta_ -
                           DirtyContribution(target) + static_cast<std::int64_t>(prospective_size) -
                           static_cast<std::int64_t>(target.persisted_size);
    if (projected < 0) {
        throw VfsError(kEio, "sandbox quota accounting is inconsistent");
    }
    if (static_cast<std::uint64_t>(projected) > sandbox_->QuotaBytes()) {
        throw VfsError(kEnospc, "sandbox byte quota exhausted by dirty VFS data");
    }
}

void VirtualFileSystem::Impl::PersistDirectoryLocked(const std::string& path) {
    if (sandbox_ == nullptr || !IsWritableNamespaceLocked(path)) return;
    // Metadata changes land immediately: there is no later point that would
    // obviously own them.
    sandbox_->CreateDirectory(path);
    tombstones_.erase(path);
}

void VirtualFileSystem::Impl::PersistRemovalLocked(const std::string& path) {
    if (sandbox_ == nullptr || !IsWritableNamespaceLocked(path)) return;
    // Always record a deletion. Once an overlay has shadowed a base file the
    // live node no longer retains enough provenance to prove that no lower
    // layer exists. A redundant tombstone for an overlay-only path is
    // harmless; omitting one can resurrect stale save data next session.
    sandbox_->WriteTombstone(path);
    tombstones_.insert(path);
}

std::int32_t VirtualFileSystem::Impl::OpenDirectory(const std::string_view path, const bool large_file,
                                                    const bool no_follow) {
    auto entries = ListDirectory(path);
    std::scoped_lock lock(mutex_);
    const auto normalized = ResolveDataPathLocked(path);
    if (files_.contains(normalized)) {
        throw VfsError(kEnotdir, "VFS path is not a directory");
    }
    if (!IsDirectoryLocked(normalized)) {
        throw VfsError(kEnoent, "VFS directory not found");
    }
    const auto descriptor = AllocateDescriptor();
    auto directory = std::make_shared<OpenDirectoryState>();
    directory->entries = std::move(entries);
    directory->info = DirectoryInfoLocked(normalized);
    directory->system_partition = IsSystemPartitionPath(normalized);
    directory->path = normalized;
    directory->spelling = PreserveSpelling(
        normalized, !path.empty() && path.front() == '/'
                        ? std::string(path)
                        : (working_directory_spelling_ == "/" ? std::string("/") : working_directory_spelling_ + "/") +
                              std::string(path));
    auto opened = std::make_shared<OpenFile>(nullptr, 0, true, false, std::move(directory));
    opened->large_file = large_file;
    opened->no_follow = no_follow;
    CurrentTableLocked().entries.emplace(descriptor, std::move(opened));
    return descriptor;
}

std::vector<VfsDirectoryEntry> VirtualFileSystem::Impl::ReadDirectory(const std::int32_t descriptor,
                                                                      const std::size_t maximum) {
    std::shared_ptr<OpenFile> open;
    {
        std::scoped_lock lock(mutex_);
        open = FindDescriptor(descriptor);
    }
    std::scoped_lock operation(open->mutex);
    if (!open->directory) {
        throw VfsError(kEnotdir, "VFS descriptor is not a directory");
    }
    auto& state = *open->directory;
    std::vector<VfsDirectoryEntry> page;
    while (state.cursor < state.entries.size() && page.size() < maximum) {
        page.push_back(state.entries[state.cursor++]);
    }
    return page;
}

VfsFileInfo VirtualFileSystem::Impl::DescriptorInfo(const std::int32_t descriptor) const {
    std::scoped_lock lock(mutex_);
    const auto& entries = CurrentTableLocked().entries;
    const auto found = entries.find(descriptor);
    // Stream endpoints keep their historical fstat behaviour: no metadata.
    if (found == entries.end() || found->second->endpoint) {
        throw VfsError(kEbadf, "VFS descriptor is not open");
    }
    const auto& open = found->second;
    if (open->directory) return open->directory->info;
    auto info = FileInfoLocked(*open->file, open->owner_id);
    if (open->pipe) info.device = static_cast<std::uint32_t>(Volume::pipefs);
    return info;
}

void VirtualFileSystem::Impl::CreateDirectory(const std::string_view path) {
    std::scoped_lock lock(mutex_);
    const auto normalized = ResolveDataPathLocked(path);
    if (files_.contains(normalized)) {
        throw VfsError(kEexist, "VFS path already holds a file");
    }
    if (IsDirectoryLocked(normalized)) {
        throw VfsError(kEexist, "VFS directory already exists");
    }
    if (IsSystemPartitionPath(normalized)) throw VfsError(kErofs, "read-only VFS system partition");
    if (sandbox_ != nullptr && !IsWritableNamespaceLocked(normalized)) {
        throw VfsError(kEacces, "VFS path is outside the writable namespace");
    }
    const auto slash = normalized.rfind('/');
    const auto parent = slash == 0 ? std::string("/") : normalized.substr(0, slash);
    if (parent != "/" && !IsDirectoryLocked(parent)) {
        throw VfsError(kEnoent, "VFS parent directory does not exist");
    }
    auto spelling = NewPathSpellingLocked(normalized, path);
    PersistDirectoryLocked(normalized);
    directories_.emplace(normalized, std::move(spelling));
}

void VirtualFileSystem::Impl::RemoveFile(const std::string_view path) {
    std::scoped_lock lock(mutex_);
    const auto normalized = ResolveDataPathLocked(path);
    const auto found = files_.find(normalized);
    if (found == files_.end() || tombstones_.contains(normalized)) {
        if (IsDirectoryLocked(normalized)) {
            throw VfsError(kEisdir, "VFS path is a directory");
        }
        throw VfsError(kEnoent, "VFS file not found");
    }
    if (IsSystemPartitionPath(normalized)) throw VfsError(kErofs, "read-only VFS system partition");
    if (!found->second->writable) {
        throw VfsError(kEacces, "VFS file is read-only");
    }
    PersistRemovalLocked(normalized);
    // Open descriptors may keep the unlinked node alive, but close/fsync on
    // that orphan must not publish the deleted pathname again.
    SetNodeSizeDirtyLocked(*found->second, found->second->size, false);
    found->second->overlay_path.clear();
    files_.erase(found);
}

void VirtualFileSystem::Impl::RemoveDirectory(const std::string_view path) {
    std::scoped_lock lock(mutex_);
    const auto normalized = ResolveDataPathLocked(path);
    if (normalized == "/") throw VfsError(kEbusy, "VFS root cannot be removed");
    if (sandbox_ != nullptr &&
        std::ranges::find(sandbox_->SemanticRoots(), normalized) != sandbox_->SemanticRoots().end()) {
        throw VfsError(kEbusy, "sandbox semantic root cannot be removed");
    }
    if (files_.contains(normalized)) {
        throw VfsError(kEnotdir, "VFS path is not a directory");
    }
    if (!IsDirectoryLocked(normalized)) {
        throw VfsError(kEnoent, "VFS directory not found");
    }
    if (IsSystemPartitionPath(normalized)) throw VfsError(kErofs, "read-only VFS system partition");
    auto prefix = normalized;
    prefix.push_back('/');
    const auto child_file = files_.lower_bound(prefix);
    if (child_file != files_.end() && child_file->first.starts_with(prefix)) {
        throw VfsError(kEnotempty, "VFS directory is not empty");
    }
    const auto child_directory = directories_.lower_bound(prefix);
    if (child_directory != directories_.end() && child_directory->first.starts_with(prefix)) {
        throw VfsError(kEnotempty, "VFS directory is not empty");
    }
    if (!directories_.contains(normalized)) {
        // Implicit directories exist only through their children, and the
        // checks above proved there are none left.
        throw VfsError(kEnoent, "VFS directory not found");
    }
    PersistRemovalLocked(normalized);
    directories_.erase(normalized);
    directory_metadata_.erase(normalized);
    directory_inodes_.erase(normalized);
    if (working_directory_ == normalized) {
        working_directory_unlinked_ = true;
        working_directory_.reset();
    }
    ForEachOpenLocked([&](std::int32_t, const std::shared_ptr<OpenFile>& open) {
        if (open->directory && open->directory->path == normalized) open->directory->unlinked = true;
    });
}

void VirtualFileSystem::Impl::Rename(const std::string_view from, const std::string_view to) {
    std::unique_lock lock(mutex_);
    const auto source = ResolveDataPathLocked(from);
    const auto target = ResolveDataPathLocked(to);
    if (source == "/") throw VfsError(kEbusy, "VFS root cannot be renamed");
    auto found = files_.find(source);
    const auto target_file = files_.find(target);
    if ((found != files_.end() && (found->second->generated || found->second->character_device)) ||
        (target_file != files_.end() && (target_file->second->generated || target_file->second->character_device)))
        throw VfsError(kEacces, "VFS provider node cannot be renamed or replaced");
    if (found == files_.end() || tombstones_.contains(source)) {
        if (source == target && IsDirectoryLocked(source)) return;
        if (IsDirectoryLocked(source)) {
            // Moving a subtree has no caller yet; guessing at it would be
            // worse than saying so.
            throw VfsError(kEinval, "VFS directory rename is not implemented");
        }
        throw VfsError(kEnoent, "VFS rename source not found");
    }
    if (source == target) return;
    if (IsSystemPartitionPath(source) || IsSystemPartitionPath(target))
        throw VfsError(kErofs, "read-only VFS system partition");
    if (!found->second->writable) {
        throw VfsError(kEacces, "VFS rename source is read-only");
    }
    if (IsDirectoryLocked(target)) {
        throw VfsError(kEisdir, "VFS rename target is a directory");
    }
    const auto target_slash = target.rfind('/');
    const auto target_parent = target_slash == 0U ? std::string("/") : target.substr(0, target_slash);
    if (!IsDirectoryLocked(target_parent)) {
        throw VfsError(kEnoent, "VFS rename target parent does not exist");
    }
    if (sandbox_ != nullptr && !IsWritableNamespaceLocked(target)) {
        throw VfsError(kEacces, "VFS path is outside the writable namespace");
    }
    auto file = found->second;
    auto spelling = NewPathSpellingLocked(target, to);
    RejectMappedMutationLocked(*file);
    if (target_file != files_.end()) RejectMappedMutationLocked(*target_file->second);
    const bool needs_materialization = sandbox_ != nullptr && file->dirty && !file->overlay_path.empty();
    lock.unlock();
    std::scoped_lock file_lock(*file->mutex);
    if (needs_materialization) Materialize(*file);
    lock.lock();
    found = files_.find(source);
    if (found == files_.end() || found->second != file || tombstones_.contains(source)) {
        throw VfsError(kEnoent, "VFS rename source changed concurrently");
    }
    if (IsDirectoryLocked(target)) {
        throw VfsError(kEisdir, "VFS rename target is a directory");
    }
    if (!IsDirectoryLocked(target_parent)) {
        throw VfsError(kEnoent, "VFS rename target parent does not exist");
    }
    if (sandbox_ != nullptr && !IsWritableNamespaceLocked(target)) {
        throw VfsError(kEacces, "VFS path is outside the writable namespace");
    }
    if (const auto current_target = files_.find(target);
        current_target != files_.end() &&
        (current_target->second->generated || current_target->second->character_device))
        throw VfsError(kEacces, "VFS provider node cannot be replaced");
    RejectMappedMutationLocked(*file);
    if (const auto replaced = files_.find(target); replaced != files_.end())
        RejectMappedMutationLocked(*replaced->second);
    auto canonical_target = target;
    files_.erase(found);
    if (const auto replaced = files_.find(target); replaced != files_.end() && replaced->second != file) {
        // The node being replaced may live on through open descriptors,
        // but a later close/fsync on that orphan must not overwrite the
        // renamed content under the same name.
        SetNodeSizeDirtyLocked(*replaced->second, replaced->second->size, false);
        replaced->second->overlay_path.clear();
    }
    files_.insert_or_assign(target, file);
    file->canonical_path = std::move(canonical_target);
    file->spelling = std::move(spelling);
    ForEachOpenLocked([&](std::int32_t, const std::shared_ptr<OpenFile>& open) {
        if (open->file == file) open->owner_id = OwnerLocked(target);
    });
    tombstones_.erase(target);
    MarkOverlayLocked(target, *file);
    SetNodeSizeDirtyLocked(*file, file->size, file->dirty || !file->overlay_path.empty());
    FlushFileLocked(*file);
    PersistRemovalLocked(source);
}

void VirtualFileSystem::Impl::Truncate(const std::int32_t descriptor, const std::uint64_t size) {
    std::shared_ptr<OpenFile> open;
    {
        std::scoped_lock lock(mutex_);
        open = FindDescriptor(descriptor);
    }
    std::scoped_lock operation(open->mutex);
    if (open->directory) {
        throw VfsError(kEisdir, "VFS descriptor is a directory");
    }
    if (!open->writable) {
        throw VfsError(kEbadf, "VFS descriptor is not writable");
    }
    if (open->pipe || open->endpoint) throw VfsError(kEinval, "VFS stream cannot be truncated");
    if (size > std::numeric_limits<std::size_t>::max()) {
        throw VfsError(kEfbig, "VFS truncate size is not representable");
    }
    // Growing has to see the backing bytes first, or the tail would be
    // zeroes over content that was never read.
    auto file = open->file;
    std::scoped_lock file_lock(*file->mutex);
    {
        std::scoped_lock lock(mutex_);
        RejectMappedMutationLocked(*file);
    }
    Materialize(*file);
    std::scoped_lock lock(mutex_);
    RejectMappedMutationLocked(*file);
    RequireSandboxQuotaLocked(*file, size);
    file->contents.resize(static_cast<std::size_t>(size));
    ++file->generation;
    SetNodeSizeDirtyLocked(*file, size, file->dirty || !file->overlay_path.empty());
}

void VirtualFileSystem::Impl::Flush(const std::int32_t descriptor) {
    std::shared_ptr<OpenFile> open;
    std::vector<std::shared_ptr<const std::function<void()>>> mapped;
    {
        std::scoped_lock lock(mutex_);
        open = FindDescriptor(descriptor);
        if (open->file) CollectMappedWritesLocked(*open->file, mapped);
    }
    WriteBackMappings(mapped);
    if (open->pipe || open->endpoint) return;
    std::scoped_lock operation(open->mutex);
    if (open->directory) return;
    if (open->file->character_device) throw VfsError(kEinval, "VFS character device cannot be flushed");
    std::scoped_lock file_lock(*open->file->mutex);
    std::scoped_lock lock(mutex_);
    FlushFileLocked(*open->file);
}

void VirtualFileSystem::Impl::FlushAll() {
    std::vector<std::shared_ptr<File>> files;
    std::vector<std::shared_ptr<const std::function<void()>>> mapped;
    {
        std::scoped_lock lock(mutex_);
        if (sandbox_ == nullptr) return;
        files.reserve(files_.size());
        for (const auto& [path, file] : files_) {
            static_cast<void>(path);
            files.push_back(file);
            CollectMappedWritesLocked(*file, mapped);
        }
    }
    WriteBackMappings(mapped);
    for (const auto& file : files) {
        std::scoped_lock file_lock(*file->mutex);
        std::scoped_lock lock(mutex_);
        FlushFileLocked(*file);
    }
    flushes_.fetch_add(1, std::memory_order_relaxed);
}

bool VirtualFileSystem::Impl::SandboxAttached() const {
    std::scoped_lock lock(mutex_);
    return sandbox_ != nullptr;
}

VfsIoStatistics VirtualFileSystem::Impl::IoStatistics() const {
    VfsIoStatistics result{
        .backing_read_bytes = backing_read_bytes_.load(std::memory_order_relaxed),
        .full_materialized_bytes = full_materialized_bytes_.load(std::memory_order_relaxed),
    };
    host_pool_->AddStatistics(result);
    std::scoped_lock lock(resource_budget_->mutex);
    result.resource_memory_bytes = resource_budget_->used;
    result.resource_memory_high_water = resource_budget_->high_water;
    result.lease_snapshot_bytes = resource_budget_->snapshot_used;
    result.lease_snapshot_high_water = resource_budget_->snapshot_high_water;
    return result;
}

std::shared_ptr<const VfsResourceReservation> VirtualFileSystem::Impl::ReserveResourceMemory(const std::uint64_t bytes,
                                                                                             const bool snapshot) {
    auto budget = resource_budget_;
    {
        std::scoped_lock lock(budget->mutex);
        if (bytes > budget->limit - budget->used) {
            throw VfsError(kEnospc, "VFS resource memory budget exhausted");
        }
        budget->used += bytes;
        budget->high_water = std::max(budget->high_water, budget->used);
        if (snapshot) {
            budget->snapshot_used += bytes;
            budget->snapshot_high_water = std::max(budget->snapshot_high_water, budget->snapshot_used);
        }
    }
    return std::shared_ptr<const VfsResourceReservation>(new VfsResourceReservation([budget, bytes, snapshot] {
        std::scoped_lock lock(budget->mutex);
        budget->used -= bytes;
        if (snapshot) budget->snapshot_used -= bytes;
    }));
}

void VirtualFileSystem::Impl::AttachSandbox(SandboxStore& store, const std::span<const std::string> writable_roots) {
    std::scoped_lock lock(mutex_);
    if (sandbox_ != nullptr) {
        throw VfsError(kEexist, "a sandbox is already attached");
    }
    if (writable_roots.empty()) {
        throw VfsError(kEinval, "a sandbox needs a writable namespace");
    }
    std::vector<std::string> roots;
    roots.reserve(writable_roots.size());
    for (const auto& root : writable_roots) {
        roots.push_back(NormalizeAbsolutePath(root, false));
    }

    // Load the overlay before publishing the store, so a malformed sandbox
    // leaves the VFS exactly as it was.
    struct Pending final {
        std::string path;
        std::string spelling;
        std::shared_ptr<File> file;
        bool is_directory{};
        bool is_tombstone{};
    };

    std::vector<Pending> pending;
    for (const auto& entry : store.Entries()) {
        const auto normalized = NormalizePath(entry.path);
        Pending item;
        item.path = normalized;
        item.spelling = PreserveSpelling(normalized, entry.path);
        item.is_directory = entry.is_directory;
        item.is_tombstone = entry.is_tombstone;
        if (!entry.is_directory && !entry.is_tombstone) {
            auto file = std::make_shared<File>();
            file->node_id = file->inode = AllocateNodeIdLocked();
            file->canonical_path = normalized;
            file->spelling = item.spelling;
            if (file->spelling == normalized) {
                if (const auto base = files_.find(normalized); base != files_.end()) {
                    file->spelling = base->second->spelling;
                }
            }
            file->size = entry.size;
            file->writable = true;
            file->source = VfsSource::sandbox;
            file->overlay_path = normalized;
            file->persisted_size = entry.size;
            const auto guest_path = entry.path;
            file->read_all = [&store, guest_path] { return store.ReadFile(guest_path); };
            item.file = std::move(file);
        }
        pending.push_back(std::move(item));
    }

    for (auto& item : pending) {
        if (item.is_directory) {
            directories_.insert_or_assign(std::move(item.path), std::move(item.spelling));
            continue;
        }
        if (item.is_tombstone) {
            // The base layer keeps its entry; resolution hides it.
            tombstones_.insert(item.path);
            files_.erase(item.path);
            // Mounted directories below a deleted one were deleted with it; a
            // sandbox holds no live entry there.
            directories_.erase(item.path);
            const auto prefix = item.path + "/";
            for (auto it = directories_.lower_bound(prefix); it != directories_.end() && it->first.starts_with(prefix);)
                it = directories_.erase(it);
            continue;
        }
        // The overlay wins over any same-path base-layer file.
        files_.insert_or_assign(item.path, std::move(item.file));
    }
    // The writable roots are directories on the platform, so they have to
    // be directories here before anything can mkdir inside them.
    auto directory_roots = roots;
    for (const auto& root : store.SemanticRoots())
        directory_roots.push_back(root);
    for (const auto& root : directory_roots) {
        for (std::size_t cursor = 1; cursor <= root.size(); ++cursor) {
            if (cursor == root.size() || root[cursor] == '/') {
                const auto prefix = root.substr(0, cursor);
                directories_.try_emplace(prefix, prefix);
            }
        }
    }
    writable_roots_ = std::move(roots);
    sandbox_ = &store;
}

}  // namespace ogplay::runtime
