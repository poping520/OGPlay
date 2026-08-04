#include "ogplay/runtime/vfs.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ogplay::runtime {
namespace {

constexpr std::int32_t kEnoent = 2;
constexpr std::int32_t kEbadf = 9;
constexpr std::int32_t kEacces = 13;
constexpr std::int32_t kEexist = 17;
constexpr std::int32_t kEnotdir = 20;
constexpr std::int32_t kEisdir = 21;
constexpr std::int32_t kEinval = 22;
constexpr std::int32_t kEfbig = 27;

[[nodiscard]] char FoldAscii(const char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

[[nodiscard]] std::string NormalizePath(const std::string_view path) {
    if (path.empty() || path.front() != '/') {
        throw VfsError(kEnotdir, "VFS path must be absolute");
    }
    std::string result;
    std::size_t cursor = 1;
    while (cursor <= path.size()) {
        const auto end = path.find('/', cursor);
        const auto count = (end == std::string_view::npos ? path.size() : end) -
                           cursor;
        const auto component = path.substr(cursor, count);
        if (component == "..") {
            throw VfsError(kEacces, "VFS path traversal is forbidden");
        }
        if (!component.empty() && component != ".") {
            result.push_back('/');
            for (const auto character : component) {
                if (character == '\0' || character == '\\') {
                    throw VfsError(kEinval, "VFS path contains an invalid character");
                }
                result.push_back(FoldAscii(character));
            }
        }
        if (end == std::string_view::npos) break;
        cursor = end + 1;
    }
    if (result.empty()) throw VfsError(kEisdir, "VFS root is not a file");
    return result;
}

struct File final {
    std::vector<std::byte> contents;
    bool writable{};
    VfsSource source{VfsSource::runtime};
};

struct OpenFile final {
    std::shared_ptr<File> file;
    std::uint64_t offset{};
    bool readable{};
    bool writable{};
};

}  // namespace

VfsError::VfsError(const std::int32_t error_number, std::string message)
    : std::runtime_error(std::move(message)), error_number_(error_number) {}

class VirtualFileSystem::Impl final {
public:
    void PutFile(const std::string_view path,
                 const std::span<const std::byte> contents,
                 const bool writable) {
        const auto normalized = NormalizePath(path);
        std::scoped_lock lock(mutex_);
        if (files_.contains(normalized)) {
            throw VfsError(kEexist, "VFS path already exists");
        }
        files_.emplace(normalized,
                       std::make_shared<File>(
                           File{std::vector(contents.begin(), contents.end()),
                                writable, VfsSource::runtime}));
    }

    void Mount(const VfsSource source, const std::string_view root,
               const std::span<const VfsMountEntry> entries) {
        if (source == VfsSource::runtime || entries.empty()) {
            throw VfsError(kEinval,
                           "VFS mount requires a source and entries");
        }
        if (root.empty() || root.front() != '/') {
            throw VfsError(kEnotdir, "VFS mount root must be absolute");
        }
        std::vector<std::pair<std::string, std::shared_ptr<File>>> pending;
        pending.reserve(entries.size());
        for (const auto& entry : entries) {
            if (entry.path.empty() || entry.path.front() == '/' ||
                entry.path.front() == '\\') {
                throw VfsError(kEinval,
                               "VFS mount entry must be relative");
            }
            auto combined = std::string(root);
            combined.push_back('/');
            combined.append(entry.path);
            const auto normalized = NormalizePath(combined);
            const auto duplicate = std::find_if(
                pending.begin(), pending.end(),
                [&normalized](const auto& candidate) {
                    return candidate.first == normalized;
                });
            if (duplicate != pending.end()) {
                throw VfsError(kEexist,
                               "VFS mount contains a duplicate path");
            }
            pending.emplace_back(
                normalized,
                std::make_shared<File>(File{
                    entry.contents, source == VfsSource::external, source}));
        }
        std::scoped_lock lock(mutex_);
        for (const auto& [path, file] : pending) {
            static_cast<void>(file);
            if (files_.contains(path)) {
                throw VfsError(kEexist, "VFS mount path already exists");
            }
        }
        for (auto& [path, file] : pending) {
            files_.emplace(std::move(path), std::move(file));
        }
    }

    [[nodiscard]] VfsFileInfo Stat(const std::string_view path) const {
        const auto normalized = NormalizePath(path);
        std::scoped_lock lock(mutex_);
        const auto found = files_.find(normalized);
        if (found == files_.end()) throw VfsError(kEnoent, "VFS file not found");
        return {found->second->contents.size(), found->second->writable,
                found->second->source};
    }

    [[nodiscard]] std::int32_t Open(const std::string_view path,
                                    const VfsOpenOptions options) {
        if (!options.read && !options.write) {
            throw VfsError(kEinval, "VFS open has no access mode");
        }
        const auto normalized = NormalizePath(path);
        std::scoped_lock lock(mutex_);
        auto found = files_.find(normalized);
        if (found == files_.end()) {
            if (!options.create) throw VfsError(kEnoent, "VFS file not found");
            found = files_.emplace(
                normalized,
                std::make_shared<File>(
                    File{{}, true, VfsSource::runtime})).first;
        }
        if (options.write && !found->second->writable) {
            throw VfsError(kEacces, "VFS file is read-only");
        }
        if (options.truncate) {
            if (!options.write) {
                throw VfsError(kEinval, "VFS truncate requires write access");
            }
            found->second->contents.clear();
        }
        const auto descriptor = AllocateDescriptor();
        descriptors_.emplace(
            descriptor,
            OpenFile{found->second, 0, options.read, options.write});
        return descriptor;
    }

    [[nodiscard]] std::size_t Read(const std::int32_t descriptor,
                                   const std::span<std::byte> destination) {
        std::scoped_lock lock(mutex_);
        auto& open = FindDescriptor(descriptor);
        if (!open.readable) throw VfsError(kEbadf, "VFS descriptor is not readable");
        const auto available = open.offset >= open.file->contents.size()
                                   ? 0
                                   : open.file->contents.size() -
                                         static_cast<std::size_t>(open.offset);
        const auto count = std::min(destination.size(), available);
        if (count != 0) {
            std::copy_n(open.file->contents.begin() +
                            static_cast<std::size_t>(open.offset),
                        count, destination.begin());
        }
        open.offset += count;
        return count;
    }

    [[nodiscard]] std::size_t Write(
        const std::int32_t descriptor,
        const std::span<const std::byte> source) {
        std::scoped_lock lock(mutex_);
        auto& open = FindDescriptor(descriptor);
        if (!open.writable) throw VfsError(kEbadf, "VFS descriptor is not writable");
        const auto end = open.offset + source.size();
        if (end > std::numeric_limits<std::size_t>::max()) {
            throw VfsError(kEfbig, "VFS file size is not representable");
        }
        if (end > open.file->contents.size()) {
            open.file->contents.resize(static_cast<std::size_t>(end));
        }
        std::copy(source.begin(), source.end(),
                  open.file->contents.begin() +
                      static_cast<std::size_t>(open.offset));
        open.offset = end;
        return source.size();
    }

    [[nodiscard]] std::uint64_t Seek(const std::int32_t descriptor,
                                     const std::int64_t offset,
                                     const VfsSeekWhence whence) {
        std::scoped_lock lock(mutex_);
        auto& open = FindDescriptor(descriptor);
        std::uint64_t base{};
        if (whence == VfsSeekWhence::current) base = open.offset;
        if (whence == VfsSeekWhence::end) base = open.file->contents.size();
        std::uint64_t result{};
        if (offset < 0) {
            const auto magnitude = static_cast<std::uint64_t>(-(offset + 1)) + 1U;
            if (magnitude > base) throw VfsError(kEinval, "negative VFS seek");
            result = base - magnitude;
        } else {
            const auto positive = static_cast<std::uint64_t>(offset);
            if (positive > std::numeric_limits<std::uint64_t>::max() - base) {
                throw VfsError(kEfbig, "VFS seek overflows");
            }
            result = base + positive;
        }
        open.offset = result;
        return result;
    }

    void Close(const std::int32_t descriptor) {
        std::scoped_lock lock(mutex_);
        if (descriptors_.erase(descriptor) != 1) {
            throw VfsError(kEbadf, "VFS descriptor is not open");
        }
    }

private:
    [[nodiscard]] std::int32_t AllocateDescriptor() const {
        for (std::int32_t descriptor = 3;
             descriptor < std::numeric_limits<std::int32_t>::max();
             ++descriptor) {
            if (!descriptors_.contains(descriptor)) return descriptor;
        }
        throw VfsError(kEbadf, "VFS descriptor table is full");
    }

    [[nodiscard]] OpenFile& FindDescriptor(const std::int32_t descriptor) {
        const auto found = descriptors_.find(descriptor);
        if (found == descriptors_.end()) {
            throw VfsError(kEbadf, "VFS descriptor is not open");
        }
        return found->second;
    }

    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<File>, std::less<>> files_;
    std::map<std::int32_t, OpenFile> descriptors_;
};

VirtualFileSystem::VirtualFileSystem() : impl_(std::make_unique<Impl>()) {}
VirtualFileSystem::~VirtualFileSystem() = default;

void VirtualFileSystem::PutFile(const std::string_view path,
                                const std::span<const std::byte> contents,
                                const bool writable) {
    impl_->PutFile(path, contents, writable);
}
void VirtualFileSystem::Mount(
    const VfsSource source, const std::string_view root,
    const std::span<const VfsMountEntry> entries) {
    impl_->Mount(source, root, entries);
}
VfsFileInfo VirtualFileSystem::Stat(const std::string_view path) const {
    return impl_->Stat(path);
}
std::int32_t VirtualFileSystem::Open(const std::string_view path,
                                     const VfsOpenOptions options) {
    return impl_->Open(path, options);
}
std::size_t VirtualFileSystem::Read(const std::int32_t descriptor,
                                    const std::span<std::byte> destination) {
    return impl_->Read(descriptor, destination);
}
std::size_t VirtualFileSystem::Write(
    const std::int32_t descriptor,
    const std::span<const std::byte> source) {
    return impl_->Write(descriptor, source);
}
std::uint64_t VirtualFileSystem::Seek(const std::int32_t descriptor,
                                      const std::int64_t offset,
                                      const VfsSeekWhence whence) {
    return impl_->Seek(descriptor, offset, whence);
}
void VirtualFileSystem::Close(const std::int32_t descriptor) {
    impl_->Close(descriptor);
}

}  // namespace ogplay::runtime
