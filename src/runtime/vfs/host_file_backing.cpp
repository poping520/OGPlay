#include "host_file_backing.h"

#include <system_error>

namespace ogplay::runtime {
namespace {
[[noreturn]] void TranslateHostError(const std::system_error& error) {
    const auto code = error.code();
    const auto number = code == std::errc::no_such_file_or_directory ? 2 :
        code == std::errc::permission_denied ? 13 :
        code == std::errc::too_many_files_open ? 24 :
        code == std::errc::too_many_files_open_in_system ? 23 :
        code == std::errc::operation_canceled ? 125 :
        code == std::errc::value_too_large ? 27 : 5;
    throw VfsError(number, std::string("VFS host backing: ") + error.what());
}

// Validate the mounted root and its descendants, without rejecting ancestors
// above the root (e.g. macOS /var -> /private/var).
void ValidateBackingPath(const std::filesystem::path& root,
                         const std::filesystem::path& path) {
    auto current = path;
    for (;;) {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error) throw std::system_error(error, "inspect host backing path");
        if (std::filesystem::is_symlink(status))
            throw VfsError(13, "VFS host backing path became a symbolic link");
        if (current == root) break;
        const auto parent = current.parent_path();
        if (parent == current || parent.empty())
            throw VfsError(13, "VFS host backing escaped its mount root");
        current = parent;
    }
}
} // namespace

HostFilePool::HostFilePool(const std::uint32_t limit) : limit_(limit) {
    if (limit == 0) throw VfsError(22, "VFS host handle budget must be positive");
}

std::uint64_t HostFilePool::AllocateKey() {
    std::scoped_lock lock(mutex_);
    return ++next_key_;
}

std::shared_ptr<const hal::HostReadFile> HostFilePool::Acquire(HostFileBacking& backing) {
    std::scoped_lock lock(mutex_);
    if (const auto found = entries_.find(backing.key_); found != entries_.end()) {
        found->second.used = ++clock_;
        return found->second.file;
    }
    if (entries_.size() >= limit_) {
        auto victim = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            // Only the pool owns an idle handle. Guest open states, leases
            // and in-flight reads all keep independent strong references.
            if (it->second.file.use_count() == 1 &&
                (victim == entries_.end() || it->second.used < victim->second.used)) victim = it;
        }
        if (victim == entries_.end())
            throw VfsError(24, "VFS host handle budget exhausted by active files or leases");
        entries_.erase(victim);
        count_.store(entries_.size(), std::memory_order_relaxed);
        evictions_.fetch_add(1, std::memory_order_relaxed);
    }
    try {
        ValidateBackingPath(backing.root_, backing.path_);
        auto file = hal::OpenHostReadFile(backing.path_);
        const auto info = file->Info();
        if (info.size != backing.size_ ||
            (backing.identity_ && info.identity != backing.identity_->identity))
            throw VfsError(5, "VFS host backing identity or size changed: " + backing.path_.string());
        entries_.emplace(backing.key_, Entry{file, ++clock_});
        backing.identity_ = info;
        count_.store(entries_.size(), std::memory_order_relaxed);
        if (entries_.size() > high_water_.load(std::memory_order_relaxed))
            high_water_.store(entries_.size(), std::memory_order_relaxed);
        return file;
    } catch (const std::system_error& error) {
        TranslateHostError(error);
    }
}

void HostFilePool::AddStatistics(VfsIoStatistics& statistics) const noexcept {
    statistics.host_file_handles = count_.load(std::memory_order_relaxed);
    statistics.host_file_handle_high_water = high_water_.load(std::memory_order_relaxed);
    statistics.host_file_handle_evictions = evictions_.load(std::memory_order_relaxed);
}

HostFileBacking::HostFileBacking(std::shared_ptr<HostFilePool> pool,
    std::filesystem::path root, std::filesystem::path path, const std::uint64_t size)
    : pool_(std::move(pool)), root_(std::filesystem::absolute(root).lexically_normal()),
      path_(std::filesystem::absolute(path).lexically_normal()), size_(size), key_(pool_->AllocateKey()) {}

std::shared_ptr<const hal::HostReadFile> HostFileBacking::Pin() { return pool_->Acquire(*this); }

std::size_t HostFileBacking::ReadAt(const std::uint64_t offset,
    const std::span<std::byte> destination, const std::stop_token stop) {
    if (stop.stop_requested()) throw VfsError(125, "VFS host read cancelled");
    try {
        const auto file = Pin();
        const auto count = file->ReadAt(offset, destination, stop);
        if (stop.stop_requested()) throw VfsError(125, "VFS host read cancelled");
        return count;
    } catch (const std::system_error& error) {
        TranslateHostError(error);
    }
}
} // namespace ogplay::runtime
