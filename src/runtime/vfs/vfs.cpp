#include "ogplay/runtime/vfs/vfs.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ogplay::runtime {
namespace {

constexpr std::int32_t kEnoent = 2;
constexpr std::int32_t kEio = 5;
constexpr std::int32_t kEbadf = 9;
constexpr std::int32_t kEacces = 13;
constexpr std::int32_t kEexist = 17;
constexpr std::int32_t kEnotdir = 20;
constexpr std::int32_t kEisdir = 21;
constexpr std::int32_t kEinval = 22;
constexpr std::int32_t kEfbig = 27;
constexpr std::int32_t kEnotempty = 39;

[[nodiscard]] std::vector<std::byte> ReadHostFile(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open host backing file: " +
                                 path.string());
    }
    const auto end = input.tellg();
    if (end < 0) {
        throw std::runtime_error("cannot size host backing file: " +
                                 path.string());
    }
    const auto size = static_cast<std::uint64_t>(end);
    if (size > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("host backing file is too large: " +
                                 path.string());
    }
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!result.empty()) {
        input.read(reinterpret_cast<char*>(result.data()),
                   static_cast<std::streamsize>(result.size()));
    }
    if (!input) {
        throw std::runtime_error("host backing file was truncated: " +
                                 path.string());
    }
    return result;
}

[[nodiscard]] char FoldAscii(const char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

[[nodiscard]] std::string NormalizeAbsolutePath(
    const std::string_view path, const bool allow_root) {
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
    if (result.empty()) {
        if (allow_root) return "/";
        throw VfsError(kEisdir, "VFS root is not a file");
    }
    return result;
}

[[nodiscard]] std::string NormalizePath(const std::string_view path) {
    return NormalizeAbsolutePath(path, false);
}

[[nodiscard]] std::string ResolvePath(
    const std::string_view path,
    const std::optional<std::string>& working_directory) {
    if (!path.empty() && path.front() == '/') return NormalizePath(path);
    if (!working_directory.has_value()) {
        throw VfsError(kEnotdir,
                       "relative VFS path requires a working directory");
    }
    auto absolute = *working_directory;
    if (absolute != "/") absolute.push_back('/');
    absolute.append(path);
    return NormalizePath(absolute);
}

struct File final {
    std::vector<std::byte> contents;
    std::uint64_t size{};
    VfsReadOnlyLoader read_all;
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
                                contents.size(), {}, writable,
                                VfsSource::runtime}));
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
                    entry.contents, entry.contents.size(), {},
                    source == VfsSource::external, source}));
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

    void MountLazy(
        const VfsSource source, const std::string_view root,
        const std::span<const VfsLazyMountEntry> entries,
        const bool writable) {
        const auto valid_source = source == VfsSource::apk ||
                                  source == VfsSource::obb ||
                                  source == VfsSource::external;
        if (!valid_source || entries.empty() ||
            (writable && source != VfsSource::external)) {
            throw VfsError(kEinval,
                           "lazy VFS mount requires a compatible source and entries");
        }
        if (root.empty() || root.front() != '/') {
            throw VfsError(kEnotdir, "VFS mount root must be absolute");
        }
        std::vector<std::pair<std::string, std::shared_ptr<File>>> pending;
        pending.reserve(entries.size());
        for (const auto& entry : entries) {
            if (entry.path.empty() || entry.path.front() == '/' ||
                entry.path.front() == '\\' || !entry.read_all ||
                entry.size > std::numeric_limits<std::size_t>::max()) {
                throw VfsError(kEinval, "lazy VFS mount entry is invalid");
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
                    {}, entry.size, entry.read_all, writable, source}));
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

    void MountHostDirectory(const std::string_view root,
                            const std::filesystem::path& directory) {
        std::vector<std::pair<std::string, std::filesystem::path>> host_paths;
        std::error_code error;
        const auto root_status = std::filesystem::symlink_status(directory, error);
        if (error || !std::filesystem::is_directory(root_status) ||
            std::filesystem::is_symlink(root_status)) {
            throw VfsError(kEnotdir,
                           "external VFS backing must be a real directory");
        }

        std::vector<VfsLazyMountEntry> entries;
        std::filesystem::recursive_directory_iterator iterator(directory, error);
        const std::filesystem::recursive_directory_iterator end;
        if (error) {
            throw VfsError(kEio,
                           "cannot enumerate external VFS backing directory");
        }
        while (iterator != end) {
            const auto path = iterator->path();
            const auto status = iterator->symlink_status(error);
            if (error) {
                throw VfsError(kEio,
                               "cannot inspect external VFS backing entry");
            }
            if (std::filesystem::is_symlink(status)) {
                throw VfsError(kEacces,
                               "external VFS backing contains a symbolic link");
            }
            if (std::filesystem::is_regular_file(status)) {
                const auto size = iterator->file_size(error);
                if (error) {
                    throw VfsError(kEio,
                                   "cannot size external VFS backing file");
                }
                const auto relative = path.lexically_relative(directory);
                if (relative.empty() || relative.is_absolute()) {
                    throw VfsError(kEacces,
                                   "external VFS backing entry escaped its root");
                }
                entries.push_back({
                    relative.generic_string(), size,
                    [path] { return ReadHostFile(path); },
                });
                auto combined = std::string(root);
                combined.push_back('/');
                combined.append(relative.generic_string());
                host_paths.emplace_back(NormalizePath(combined), path);
            } else if (!std::filesystem::is_directory(status)) {
                throw VfsError(kEinval,
                               "external VFS backing contains a special file");
            }
            iterator.increment(error);
            if (error) {
                throw VfsError(kEio,
                               "cannot continue external VFS backing enumeration");
            }
        }
        if (entries.empty()) {
            throw VfsError(kEinval,
                           "external VFS backing directory has no files");
        }
        MountLazy(VfsSource::external, root, entries, true);
        std::scoped_lock lock(mutex_);
        for (auto& [guest_path, host_path] : host_paths) {
            host_backed_.insert_or_assign(std::move(guest_path),
                                          std::move(host_path));
        }
    }

    [[nodiscard]] std::optional<std::filesystem::path> HostPathFor(
        const std::string_view path) const {
        std::scoped_lock lock(mutex_);
        const auto normalized = ResolvePath(path, working_directory_);
        const auto found = host_backed_.find(normalized);
        if (found == host_backed_.end()) return std::nullopt;
        return found->second;
    }

    void SetWorkingDirectory(const std::string_view path) {
        const auto normalized = NormalizeAbsolutePath(path, true);
        std::scoped_lock lock(mutex_);
        working_directory_ = normalized;
    }

    [[nodiscard]] std::optional<std::string> WorkingDirectory() const {
        std::scoped_lock lock(mutex_);
        return working_directory_;
    }

    [[nodiscard]] VfsFileInfo Stat(const std::string_view path) const {
        std::scoped_lock lock(mutex_);
        const auto normalized = ResolvePath(path, working_directory_);
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

    [[nodiscard]] std::vector<VfsDirectoryEntry> ListDirectory(
        const std::string_view path) const {
        std::scoped_lock lock(mutex_);
        auto prefix = ResolvePath(path, working_directory_);
        if (prefix.empty() || prefix.back() != '/') prefix.push_back('/');
        // Merged from both indexes and deduplicated by name, so getdents64
        // sees one stable order regardless of how a directory came to be.
        std::map<std::string, bool, std::less<>> children;
        const auto collect = [&prefix, &children](const std::string& key,
                                                  const bool leaf_is_directory) {
            if (!key.starts_with(prefix)) return;
            const auto remainder = std::string_view(key).substr(prefix.size());
            if (remainder.empty()) return;
            const auto slash = remainder.find('/');
            auto name = std::string(remainder.substr(0, slash));
            const bool is_directory =
                slash != std::string_view::npos || leaf_is_directory;
            auto& recorded = children[std::move(name)];
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

    void CreateDirectory(const std::string_view path) {
        std::scoped_lock lock(mutex_);
        const auto normalized = ResolvePath(path, working_directory_);
        if (files_.contains(normalized)) {
            throw VfsError(kEexist, "VFS path already holds a file");
        }
        if (IsDirectoryLocked(normalized)) {
            throw VfsError(kEexist, "VFS directory already exists");
        }
        const auto slash = normalized.rfind('/');
        const auto parent = slash == 0 ? std::string("/")
                                       : normalized.substr(0, slash);
        if (parent != "/" && !IsDirectoryLocked(parent)) {
            throw VfsError(kEnoent, "VFS parent directory does not exist");
        }
        directories_.insert(normalized);
    }

    void RemoveFile(const std::string_view path) {
        std::scoped_lock lock(mutex_);
        const auto normalized = ResolvePath(path, working_directory_);
        const auto found = files_.find(normalized);
        if (found == files_.end()) {
            if (IsDirectoryLocked(normalized)) {
                throw VfsError(kEisdir, "VFS path is a directory");
            }
            throw VfsError(kEnoent, "VFS file not found");
        }
        if (!found->second->writable) {
            throw VfsError(kEacces, "VFS file is read-only");
        }
        files_.erase(found);
    }

    void RemoveDirectory(const std::string_view path) {
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
        if (child_file != files_.end() &&
            child_file->first.starts_with(prefix)) {
            throw VfsError(kEnotempty, "VFS directory is not empty");
        }
        const auto child_directory = directories_.lower_bound(prefix);
        if (child_directory != directories_.end() &&
            child_directory->starts_with(prefix)) {
            throw VfsError(kEnotempty, "VFS directory is not empty");
        }
        if (directories_.erase(normalized) == 0) {
            // Implicit directories exist only through their children, and
            // the checks above proved there are none left.
            throw VfsError(kEnoent, "VFS directory not found");
        }
    }

    void Rename(const std::string_view from, const std::string_view to) {
        std::scoped_lock lock(mutex_);
        const auto source = ResolvePath(from, working_directory_);
        const auto target = ResolvePath(to, working_directory_);
        if (source == target) return;
        const auto found = files_.find(source);
        if (found == files_.end()) {
            if (IsDirectoryLocked(source)) {
                // Moving a subtree has no caller yet; guessing at it would
                // be worse than saying so.
                throw VfsError(kEinval,
                               "VFS directory rename is not implemented");
            }
            throw VfsError(kEnoent, "VFS rename source not found");
        }
        if (!found->second->writable) {
            throw VfsError(kEacces, "VFS rename source is read-only");
        }
        if (IsDirectoryLocked(target)) {
            throw VfsError(kEisdir, "VFS rename target is a directory");
        }
        auto file = found->second;
        files_.erase(found);
        files_.insert_or_assign(target, std::move(file));
    }

    [[nodiscard]] std::int32_t Open(const std::string_view path,
                                    const VfsOpenOptions options) {
        if (!options.read && !options.write) {
            throw VfsError(kEinval, "VFS open has no access mode");
        }
        std::scoped_lock lock(mutex_);
        const auto normalized = ResolvePath(path, working_directory_);
        auto found = files_.find(normalized);
        if (found == files_.end()) {
            if (!options.create) throw VfsError(kEnoent, "VFS file not found");
            found = files_.emplace(
                normalized,
                    std::make_shared<File>(
                    File{{}, 0, {}, true, VfsSource::runtime})).first;
        }
        if (options.write && !found->second->writable) {
            throw VfsError(kEacces, "VFS file is read-only");
        }
        if (options.truncate) {
            if (!options.write) {
                throw VfsError(kEinval, "VFS truncate requires write access");
            }
            found->second->contents.clear();
            found->second->size = 0;
            found->second->read_all = {};
        }
        const auto descriptor = AllocateDescriptor();
        descriptors_.emplace(
            descriptor,
            OpenFile{found->second, 0, options.read, options.write});
        return descriptor;
    }

    [[nodiscard]] VfsPipeDescriptors CreatePipe() {
        std::scoped_lock lock(mutex_);
        auto pipe = std::make_shared<File>(
            File{{}, 0, {}, true, VfsSource::runtime});
        const auto read_descriptor = AllocateDescriptor();
        descriptors_.emplace(
            read_descriptor, OpenFile{pipe, 0, true, false});
        const auto write_descriptor = AllocateDescriptor();
        descriptors_.emplace(
            write_descriptor, OpenFile{std::move(pipe), 0, false, true});
        return {read_descriptor, write_descriptor};
    }

    [[nodiscard]] std::size_t Read(const std::int32_t descriptor,
                                   const std::span<std::byte> destination) {
        std::scoped_lock lock(mutex_);
        auto& open = FindDescriptor(descriptor);
        if (!open.readable) throw VfsError(kEbadf, "VFS descriptor is not readable");
        Materialize(*open.file);
        const auto available = open.offset >= open.file->size
                                   ? 0
                                   : open.file->size - open.offset;
        const auto count = std::min<std::uint64_t>(destination.size(), available);
        if (count != 0) {
            using Difference =
                std::vector<std::byte>::difference_type;
            std::copy_n(open.file->contents.begin() +
                            static_cast<Difference>(open.offset),
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
        Materialize(*open.file);
        const auto end = open.offset + source.size();
        if (end > std::numeric_limits<std::size_t>::max()) {
            throw VfsError(kEfbig, "VFS file size is not representable");
        }
        if (end > open.file->contents.size()) {
            open.file->contents.resize(static_cast<std::size_t>(end));
        }
        using Difference = std::vector<std::byte>::difference_type;
        std::copy(source.begin(), source.end(),
                  open.file->contents.begin() +
                      static_cast<Difference>(open.offset));
        open.offset = end;
        open.file->size = open.file->contents.size();
        return source.size();
    }

    [[nodiscard]] std::uint64_t Seek(const std::int32_t descriptor,
                                     const std::int64_t offset,
                                     const VfsSeekWhence whence) {
        std::scoped_lock lock(mutex_);
        auto& open = FindDescriptor(descriptor);
        std::uint64_t base{};
        if (whence == VfsSeekWhence::current) base = open.offset;
        if (whence == VfsSeekWhence::end) base = open.file->size;
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

    void Truncate(const std::int32_t descriptor, const std::uint64_t size) {
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
    }

    void Flush(const std::int32_t descriptor) {
        std::scoped_lock lock(mutex_);
        static_cast<void>(FindDescriptor(descriptor));
    }

    void FlushAll() { std::scoped_lock lock(mutex_); }

    void Close(const std::int32_t descriptor) {
        std::scoped_lock lock(mutex_);
        if (descriptors_.erase(descriptor) != 1) {
            throw VfsError(kEbadf, "VFS descriptor is not open");
        }
    }

private:
    static void Materialize(File& file) {
        if (!file.read_all) return;
        std::vector<std::byte> contents;
        try {
            contents = file.read_all();
        } catch (const std::exception& error) {
            throw VfsError(kEio,
                           std::string("VFS backing read failed: ") + error.what());
        } catch (...) {
            throw VfsError(kEio, "VFS backing read failed");
        }
        if (contents.size() != file.size) {
            throw VfsError(kEio,
                           "VFS backing size differs from mounted metadata");
        }
        file.contents = std::move(contents);
        file.read_all = {};
    }

    [[nodiscard]] std::int32_t AllocateDescriptor() const {
        for (std::int32_t descriptor = 3;
             descriptor < std::numeric_limits<std::int32_t>::max();
             ++descriptor) {
            if (!descriptors_.contains(descriptor)) return descriptor;
        }
        throw VfsError(kEbadf, "VFS descriptor table is full");
    }

    // A path is a directory when it was created explicitly or when some
    // file lives beneath it. Callers hold mutex_.
    [[nodiscard]] bool IsDirectoryLocked(const std::string& path) const {
        if (path == "/") return true;
        if (directories_.contains(path)) return true;
        auto prefix = path;
        prefix.push_back('/');
        const auto file = files_.lower_bound(prefix);
        if (file != files_.end() && file->first.starts_with(prefix)) {
            return true;
        }
        const auto directory = directories_.lower_bound(prefix);
        return directory != directories_.end() &&
               directory->starts_with(prefix);
    }

    [[nodiscard]] OpenFile& FindDescriptor(const std::int32_t descriptor) {
        const auto found = descriptors_.find(descriptor);
        if (found == descriptors_.end()) {
            throw VfsError(kEbadf, "VFS descriptor is not open");
        }
        return found->second;
    }

    mutable std::mutex mutex_;
    std::optional<std::string> working_directory_;
    std::map<std::string, std::shared_ptr<File>, std::less<>> files_;
    // Directories created explicitly; implicit ones come from files_.
    std::set<std::string, std::less<>> directories_;
    std::map<std::int32_t, OpenFile> descriptors_;
    // Guest path -> backing host file for host-directory mounts, so media
    // decoders can open the real file directly.
    std::map<std::string, std::filesystem::path, std::less<>> host_backed_;
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
void VirtualFileSystem::MountLazyReadOnly(
    const VfsSource source, const std::string_view root,
    const std::span<const VfsLazyMountEntry> entries) {
    if (source != VfsSource::apk && source != VfsSource::obb) {
        throw VfsError(kEinval,
                       "lazy read-only VFS mount requires APK or OBB source");
    }
    impl_->MountLazy(source, root, entries, false);
}
void VirtualFileSystem::MountHostDirectory(
    const std::string_view root, const std::filesystem::path& directory) {
    impl_->MountHostDirectory(root, directory);
}
std::optional<std::filesystem::path> VirtualFileSystem::HostPathFor(
    const std::string_view path) const {
    return impl_->HostPathFor(path);
}
void VirtualFileSystem::SetWorkingDirectory(const std::string_view path) {
    impl_->SetWorkingDirectory(path);
}
std::optional<std::string> VirtualFileSystem::WorkingDirectory() const {
    return impl_->WorkingDirectory();
}
VfsFileInfo VirtualFileSystem::Stat(const std::string_view path) const {
    return impl_->Stat(path);
}
std::vector<VfsDirectoryEntry> VirtualFileSystem::ListDirectory(
    const std::string_view path) const {
    return impl_->ListDirectory(path);
}
void VirtualFileSystem::CreateDirectory(const std::string_view path) {
    impl_->CreateDirectory(path);
}
void VirtualFileSystem::RemoveFile(const std::string_view path) {
    impl_->RemoveFile(path);
}
void VirtualFileSystem::RemoveDirectory(const std::string_view path) {
    impl_->RemoveDirectory(path);
}
void VirtualFileSystem::Rename(const std::string_view from,
                               const std::string_view to) {
    impl_->Rename(from, to);
}
std::int32_t VirtualFileSystem::Open(const std::string_view path,
                                     const VfsOpenOptions options) {
    return impl_->Open(path, options);
}
VfsPipeDescriptors VirtualFileSystem::CreatePipe() {
    return impl_->CreatePipe();
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
void VirtualFileSystem::Truncate(const std::int32_t descriptor,
                                 const std::uint64_t size) {
    impl_->Truncate(descriptor, size);
}
void VirtualFileSystem::Flush(const std::int32_t descriptor) {
    impl_->Flush(descriptor);
}
void VirtualFileSystem::FlushAll() { impl_->FlushAll(); }
void VirtualFileSystem::Close(const std::int32_t descriptor) {
    impl_->Close(descriptor);
}

}  // namespace ogplay::runtime
