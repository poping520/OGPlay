// Directory/metadata operations and the per-title sandbox overlay
// (ADR-0020, docs/design/sandbox/ 02 §1 and 03 §2). Resolution order is
// overlay file, then overlay tombstone (absent), then the read-only base
// layer; writes inside the writable namespace land in the overlay and reach
// the host at the enumerated flush points.

#include <algorithm>
#include <limits>
#include <utility>

#include "vfs_internal.h"

namespace ogplay::runtime {

VfsFileInfo VirtualFileSystem::Impl::Stat(const std::string_view path) const {
    std::scoped_lock lock(mutex_);
    const auto normalized = ResolvePath(path, working_directory_);
    if (tombstones_.contains(normalized)) {
        throw VfsError(kEnoent, "VFS file was deleted");
    }
    const auto found = files_.find(normalized);
    if (found != files_.end()) {
        return {found->second->size, found->second->writable,
                found->second->source, false};
    }
    if (IsDirectoryLocked(normalized)) {
        return {0, true, VfsSource::runtime, true};
    }
    throw VfsError(kEnoent, "VFS file not found");
}

std::vector<VfsDirectoryEntry> VirtualFileSystem::Impl::ListDirectory(
    const std::string_view path) const {
    std::scoped_lock lock(mutex_);
    auto prefix = ResolvePath(path, working_directory_);
    if (prefix.empty() || prefix.back() != '/') prefix.push_back('/');
    // Merged from both indexes and deduplicated by name, so getdents64 sees
    // one stable order regardless of how a directory came to be.
    std::map<std::string, bool, std::less<>> children;
    const auto collect = [&](const std::string& key,
                             const bool leaf_is_directory) {
        if (!key.starts_with(prefix)) return;
        const auto remainder = std::string_view(key).substr(prefix.size());
        if (remainder.empty()) return;
        const auto slash = remainder.find('/');
        const bool is_directory =
            slash != std::string_view::npos || leaf_is_directory;
        if (!is_directory && tombstones_.contains(key)) return;
        auto& recorded = children[std::string(remainder.substr(0, slash))];
        recorded = recorded || is_directory;
    };
    for (auto it = files_.lower_bound(prefix);
         it != files_.end() && it->first.starts_with(prefix); ++it) {
        collect(it->first, false);
    }
    for (auto it = directories_.lower_bound(prefix);
         it != directories_.end() && it->starts_with(prefix); ++it) {
        collect(*it, true);
    }
    std::vector<VfsDirectoryEntry> entries;
    entries.reserve(children.size());
    for (auto& [name, is_directory] : children) {
        entries.push_back({name, is_directory});
    }
    return entries;
}

bool VirtualFileSystem::Impl::IsDirectoryLocked(const std::string& path) const {
    if (path == "/") return true;
    if (directories_.contains(path)) return true;
    auto prefix = path;
    prefix.push_back('/');
    const auto file = files_.lower_bound(prefix);
    if (file != files_.end() && file->first.starts_with(prefix)) return true;
    const auto directory = directories_.lower_bound(prefix);
    return directory != directories_.end() && directory->starts_with(prefix);
}

bool VirtualFileSystem::Impl::IsWritableNamespaceLocked(
    const std::string& path) const {
    for (const auto& root : writable_roots_) {
        if (path == root) return true;
        if (path.starts_with(root) && path.size() > root.size() &&
            path[root.size()] == '/') {
            return true;
        }
    }
    return false;
}

void VirtualFileSystem::Impl::MarkOverlayLocked(const std::string& path,
                                                File& file) {
    if (sandbox_ == nullptr || !IsWritableNamespaceLocked(path)) return;
    file.dirty = true;
    file.overlay_path = path;
}

void VirtualFileSystem::Impl::FlushFileLocked(File& file) {
    if (sandbox_ == nullptr || !file.dirty || file.overlay_path.empty()) {
        return;
    }
    Materialize(file);
    sandbox_->WriteFileAtomic(file.overlay_path, file.contents);
    tombstones_.erase(file.overlay_path);
    file.dirty = false;
}

void VirtualFileSystem::Impl::PersistDirectoryLocked(const std::string& path) {
    if (sandbox_ == nullptr || !IsWritableNamespaceLocked(path)) return;
    // Metadata changes land immediately: there is no later point that would
    // obviously own them.
    sandbox_->CreateDirectory(path);
    tombstones_.erase(path);
}

void VirtualFileSystem::Impl::PersistRemovalLocked(const std::string& path,
                                                   const bool had_base_layer) {
    if (sandbox_ == nullptr || !IsWritableNamespaceLocked(path)) return;
    if (had_base_layer) {
        // A read-only layer still provides this path, so the deletion has to
        // be recorded or the old file would come back next session.
        sandbox_->WriteTombstone(path);
        tombstones_.insert(path);
        return;
    }
    try {
        sandbox_->Remove(path);
    } catch (const VfsError& error) {
        if (error.ErrorNumber() != kEnoent) throw;
    }
}

std::int32_t VirtualFileSystem::Impl::OpenDirectory(
    const std::string_view path) {
    auto entries = ListDirectory(path);
    std::scoped_lock lock(mutex_);
    const auto normalized = ResolvePath(path, working_directory_);
    if (files_.contains(normalized)) {
        throw VfsError(kEnotdir, "VFS path is not a directory");
    }
    if (!IsDirectoryLocked(normalized)) {
        throw VfsError(kEnoent, "VFS directory not found");
    }
    const auto descriptor = AllocateDescriptor();
    OpenFile open;
    open.readable = true;
    open.directory = std::make_shared<OpenDirectoryState>();
    open.directory->entries = std::move(entries);
    descriptors_.emplace(descriptor, std::move(open));
    return descriptor;
}

std::vector<VfsDirectoryEntry> VirtualFileSystem::Impl::ReadDirectory(
    const std::int32_t descriptor, const std::size_t maximum) {
    std::scoped_lock lock(mutex_);
    auto& open = FindDescriptor(descriptor);
    if (!open.directory) {
        throw VfsError(kEnotdir, "VFS descriptor is not a directory");
    }
    auto& state = *open.directory;
    std::vector<VfsDirectoryEntry> page;
    while (state.cursor < state.entries.size() && page.size() < maximum) {
        page.push_back(state.entries[state.cursor++]);
    }
    return page;
}

void VirtualFileSystem::Impl::CreateDirectory(const std::string_view path) {
    std::scoped_lock lock(mutex_);
    const auto normalized = ResolvePath(path, working_directory_);
    if (files_.contains(normalized)) {
        throw VfsError(kEexist, "VFS path already holds a file");
    }
    if (IsDirectoryLocked(normalized)) {
        throw VfsError(kEexist, "VFS directory already exists");
    }
    const auto slash = normalized.rfind('/');
    const auto parent =
        slash == 0 ? std::string("/") : normalized.substr(0, slash);
    if (parent != "/" && !IsDirectoryLocked(parent)) {
        throw VfsError(kEnoent, "VFS parent directory does not exist");
    }
    if (sandbox_ != nullptr && !IsWritableNamespaceLocked(normalized)) {
        throw VfsError(kEacces, "VFS path is outside the writable namespace");
    }
    PersistDirectoryLocked(normalized);
    directories_.insert(normalized);
}

void VirtualFileSystem::Impl::RemoveFile(const std::string_view path) {
    std::scoped_lock lock(mutex_);
    const auto normalized = ResolvePath(path, working_directory_);
    const auto found = files_.find(normalized);
    if (found == files_.end() || tombstones_.contains(normalized)) {
        if (IsDirectoryLocked(normalized)) {
            throw VfsError(kEisdir, "VFS path is a directory");
        }
        throw VfsError(kEnoent, "VFS file not found");
    }
    if (!found->second->writable) {
        throw VfsError(kEacces, "VFS file is read-only");
    }
    // A node still carrying a base-layer loader would reappear next session
    // unless the deletion is tombstoned.
    const bool had_base_layer = found->second->source != VfsSource::runtime &&
                                found->second->overlay_path.empty();
    PersistRemovalLocked(normalized, had_base_layer);
    files_.erase(found);
}

void VirtualFileSystem::Impl::RemoveDirectory(const std::string_view path) {
    std::scoped_lock lock(mutex_);
    const auto normalized = ResolvePath(path, working_directory_);
    if (files_.contains(normalized)) {
        throw VfsError(kEnotdir, "VFS path is not a directory");
    }
    if (!IsDirectoryLocked(normalized)) {
        throw VfsError(kEnoent, "VFS directory not found");
    }
    auto prefix = normalized;
    prefix.push_back('/');
    const auto child_file = files_.lower_bound(prefix);
    if (child_file != files_.end() && child_file->first.starts_with(prefix)) {
        throw VfsError(kEnotempty, "VFS directory is not empty");
    }
    const auto child_directory = directories_.lower_bound(prefix);
    if (child_directory != directories_.end() &&
        child_directory->starts_with(prefix)) {
        throw VfsError(kEnotempty, "VFS directory is not empty");
    }
    if (directories_.erase(normalized) == 0) {
        // Implicit directories exist only through their children, and the
        // checks above proved there are none left.
        throw VfsError(kEnoent, "VFS directory not found");
    }
    PersistRemovalLocked(normalized, false);
}

void VirtualFileSystem::Impl::Rename(const std::string_view from,
                                     const std::string_view to) {
    std::scoped_lock lock(mutex_);
    const auto source = ResolvePath(from, working_directory_);
    const auto target = ResolvePath(to, working_directory_);
    if (source == target) return;
    const auto found = files_.find(source);
    if (found == files_.end() || tombstones_.contains(source)) {
        if (IsDirectoryLocked(source)) {
            // Moving a subtree has no caller yet; guessing at it would be
            // worse than saying so.
            throw VfsError(kEinval, "VFS directory rename is not implemented");
        }
        throw VfsError(kEnoent, "VFS rename source not found");
    }
    if (!found->second->writable) {
        throw VfsError(kEacces, "VFS rename source is read-only");
    }
    if (IsDirectoryLocked(target)) {
        throw VfsError(kEisdir, "VFS rename target is a directory");
    }
    if (sandbox_ != nullptr && !IsWritableNamespaceLocked(target)) {
        throw VfsError(kEacces, "VFS path is outside the writable namespace");
    }
    const bool had_base_layer = found->second->source != VfsSource::runtime &&
                                found->second->overlay_path.empty();
    auto file = found->second;
    files_.erase(found);
    files_.insert_or_assign(target, file);
    tombstones_.erase(target);
    MarkOverlayLocked(target, *file);
    FlushFileLocked(*file);
    PersistRemovalLocked(source, had_base_layer);
}

void VirtualFileSystem::Impl::Truncate(const std::int32_t descriptor,
                                       const std::uint64_t size) {
    std::scoped_lock lock(mutex_);
    auto& open = FindDescriptor(descriptor);
    if (!open.writable) {
        throw VfsError(kEbadf, "VFS descriptor is not writable");
    }
    if (size > std::numeric_limits<std::size_t>::max()) {
        throw VfsError(kEfbig, "VFS truncate size is not representable");
    }
    // Growing has to see the backing bytes first, or the tail would be
    // zeroes over content that was never read.
    Materialize(*open.file);
    open.file->contents.resize(static_cast<std::size_t>(size));
    open.file->size = size;
    if (!open.file->overlay_path.empty()) open.file->dirty = true;
}

void VirtualFileSystem::Impl::Flush(const std::int32_t descriptor) {
    std::scoped_lock lock(mutex_);
    auto& open = FindDescriptor(descriptor);
    FlushFileLocked(*open.file);
}

void VirtualFileSystem::Impl::FlushAll() {
    std::scoped_lock lock(mutex_);
    if (sandbox_ == nullptr) return;
    for (auto& [path, file] : files_) {
        static_cast<void>(path);
        FlushFileLocked(*file);
    }
}

bool VirtualFileSystem::Impl::SandboxAttached() const {
    std::scoped_lock lock(mutex_);
    return sandbox_ != nullptr;
}

void VirtualFileSystem::Impl::AttachSandbox(
    SandboxStore& store, const std::span<const std::string> writable_roots) {
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
        std::shared_ptr<File> file;
        bool is_directory{};
        bool is_tombstone{};
    };
    std::vector<Pending> pending;
    for (const auto& entry : store.Entries()) {
        const auto normalized = NormalizePath(entry.path);
        Pending item;
        item.path = normalized;
        item.is_directory = entry.is_directory;
        item.is_tombstone = entry.is_tombstone;
        if (!entry.is_directory && !entry.is_tombstone) {
            auto file = std::make_shared<File>();
            file->size = entry.size;
            file->writable = true;
            file->source = VfsSource::sandbox;
            file->overlay_path = normalized;
            const auto guest_path = entry.path;
            file->read_all = [&store, guest_path] {
                return store.ReadFile(guest_path);
            };
            item.file = std::move(file);
        }
        pending.push_back(std::move(item));
    }

    for (auto& item : pending) {
        if (item.is_directory) {
            directories_.insert(item.path);
            continue;
        }
        if (item.is_tombstone) {
            // The base layer keeps its entry; resolution hides it.
            tombstones_.insert(item.path);
            files_.erase(item.path);
            continue;
        }
        // The overlay wins over any same-path base-layer file.
        files_.insert_or_assign(item.path, std::move(item.file));
    }
    // The writable roots are directories on the platform, so they have to
    // be directories here before anything can mkdir inside them.
    for (const auto& root : roots) {
        for (std::size_t cursor = 1; cursor <= root.size(); ++cursor) {
            if (cursor == root.size() || root[cursor] == '/') {
                directories_.insert(root.substr(0, cursor));
            }
        }
    }
    writable_roots_ = std::move(roots);
    sandbox_ = &store;
}

}  // namespace ogplay::runtime
