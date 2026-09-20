#include "vfs_internal.h"

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

class HostFileReader final {
public:
    explicit HostFileReader(const std::filesystem::path& path)
        : input_(path, std::ios::binary) {
        if (!input_) {
            throw std::runtime_error("cannot open host backing file: " +
                                     path.string());
        }
    }

    std::size_t ReadAt(const std::uint64_t offset,
                       const std::span<std::byte> destination) {
        std::unique_lock lock(mutex_);
        input_.clear();
        input_.seekg(static_cast<std::streamoff>(offset));
        if (!input_) throw std::runtime_error("cannot seek host backing file");
        input_.read(reinterpret_cast<char*>(destination.data()),
                    static_cast<std::streamsize>(destination.size()));
        const auto count = static_cast<std::size_t>(input_.gcount());
        if (count != destination.size() && !input_.eof()) {
            throw std::runtime_error("cannot read host backing file");
        }
        return count;
    }

private:
    std::mutex mutex_;
    std::ifstream input_;
};

class BackingLease final : public VfsReadLease {
public:
    BackingLease(std::shared_ptr<File> file, const std::uint64_t begin,
                 const std::uint64_t length)
        : file_(std::move(file)), begin_(begin), length_(length) {}
    std::uint64_t Size() const noexcept override { return length_; }
    std::size_t ReadAt(const std::uint64_t offset,
                       const std::span<std::byte> destination,
                       const std::stop_token stop = {}) const override {
        if (stop.stop_requested()) throw VfsError(kEcanceled, "VFS read cancelled");
        if (offset >= length_) return 0;
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            destination.size(), length_ - offset));
        std::scoped_lock lock(*file_->mutex);
        auto output = destination.first(count);
        if (file_->read_at) {
            const auto got = file_->read_at(begin_ + offset, output, stop);
            if (stop.stop_requested()) {
                throw VfsError(kEcanceled, "VFS read cancelled");
            }
            if (got != count) throw VfsError(kEio, "VFS lease backing was truncated");
            return got;
        }
        if (file_->read_all) {
            auto contents = file_->read_all();
            if (contents.size() != file_->size)
                throw VfsError(kEio, "VFS lease backing size mismatch");
            file_->contents = std::move(contents);
            file_->read_all = {};
            file_->read_at = {};
        }
        using Difference = std::vector<std::byte>::difference_type;
        std::copy_n(file_->contents.begin() +
                        static_cast<Difference>(begin_ + offset),
                    count, output.begin());
        return count;
    }
private:
    std::shared_ptr<File> file_;
    std::uint64_t begin_{};
    std::uint64_t length_{};
};

class SnapshotLease final : public VfsReadLease {
public:
    SnapshotLease(std::vector<std::byte> bytes,
                  std::shared_ptr<const VfsResourceReservation> reservation)
        : bytes_(std::move(bytes)), reservation_(std::move(reservation)) {}
    std::uint64_t Size() const noexcept override { return bytes_.size(); }
    std::size_t ReadAt(const std::uint64_t offset,
                       const std::span<std::byte> destination,
                       const std::stop_token stop = {}) const override {
        if (stop.stop_requested()) throw VfsError(kEcanceled, "VFS read cancelled");
        if (offset >= bytes_.size()) return 0;
        const auto count = std::min<std::size_t>(
            destination.size(), bytes_.size() - static_cast<std::size_t>(offset));
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), count,
                    destination.begin());
        return count;
    }
private:
    std::vector<std::byte> bytes_;
    std::shared_ptr<const VfsResourceReservation> reservation_;
};

[[nodiscard]] char FoldAscii(const char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

std::string NormalizeAbsolutePath(
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

std::string NormalizePath(const std::string_view path) {
    return NormalizeAbsolutePath(path, false);
}

std::string ResolvePath(
    const std::string_view path,
    const std::optional<std::string>& working_directory,
    const std::map<std::string, std::string, std::less<>>& aliases) {
    std::string normalized;
    if (!path.empty() && path.front() == '/') {
        normalized = NormalizePath(path);
    } else {
        if (!working_directory.has_value()) {
            throw VfsError(kEnotdir,
                           "relative VFS path requires a working directory");
        }
        auto absolute = *working_directory;
        if (absolute != "/") absolute.push_back('/');
        absolute.append(path);
        normalized = NormalizePath(absolute);
    }
    for (const auto& [alias, target] : aliases) {
        if (normalized == alias) return target;
        if (normalized.size() > alias.size() &&
            normalized.starts_with(alias) &&
            normalized[alias.size()] == '/') {
            return target + normalized.substr(alias.size());
        }
    }
    return normalized;
}

VfsError::VfsError(const std::int32_t error_number, std::string message)
    : std::runtime_error(std::move(message)), error_number_(error_number) {}

void VirtualFileSystem::Impl::PutFile(const std::string_view path,
                 const std::span<const std::byte> contents,
                 const bool writable) {
        const auto normalized = NormalizePath(path);
        std::scoped_lock lock(mutex_);
        if (files_.contains(normalized)) {
            throw VfsError(kEexist, "VFS path already exists");
        }
        auto file = std::make_shared<File>(
                           File{std::vector(contents.begin(), contents.end()),
                                contents.size(), {}, {}, writable,
                                VfsSource::runtime, false, {}});
        file->node_id = next_node_id_++;
        files_.emplace(normalized, std::move(file));
    }

void VirtualFileSystem::Impl::Mount(const VfsSource source, const std::string_view root,
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
                    entry.contents, entry.contents.size(), {}, {},
                    source == VfsSource::external, source, false, {}}));
        }
        std::scoped_lock lock(mutex_);
        for (const auto& [path, file] : pending) {
            static_cast<void>(file);
            if (files_.contains(path)) {
                throw VfsError(kEexist, "VFS mount path already exists");
            }
        }
        for (auto& [path, file] : pending) {
            file->node_id = next_node_id_++;
            files_.emplace(std::move(path), std::move(file));
        }
    }

void VirtualFileSystem::Impl::MountLazy(
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
                    {}, entry.size, entry.read_all, entry.read_at, writable, source,
                    false, {}}));
        }
        std::scoped_lock lock(mutex_);
        for (const auto& [path, file] : pending) {
            static_cast<void>(file);
            if (files_.contains(path)) {
                throw VfsError(kEexist, "VFS mount path already exists");
            }
        }
        for (auto& [path, file] : pending) {
            file->node_id = next_node_id_++;
            files_.emplace(std::move(path), std::move(file));
        }
    }

void VirtualFileSystem::Impl::MountHostDirectory(const std::string_view root,
                            const std::filesystem::path& directory) {
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
                const auto reader = std::make_shared<HostFileReader>(path);
                entries.push_back({
                    relative.generic_string(), size,
                    [reader, size] {
                        if (size > std::numeric_limits<std::size_t>::max()) {
                            throw std::runtime_error(
                                "host backing file is too large to materialize");
                        }
                        std::vector<std::byte> result(
                            static_cast<std::size_t>(size));
                        if (reader->ReadAt(0, result) != result.size()) {
                            throw std::runtime_error(
                                "host backing file was truncated");
                        }
                        return result;
                    },
                    [reader](const std::uint64_t offset,
                             const std::span<std::byte> destination) {
                        return reader->ReadAt(offset, destination);
                    },
                });
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
    }

void VirtualFileSystem::Impl::SetWorkingDirectory(const std::string_view path) {
        std::scoped_lock lock(mutex_);
        const auto normalized = path == "/"
                                    ? std::string("/")
                                    : ResolvePath(path, std::nullopt, aliases_);
        working_directory_ = normalized;
    }

void VirtualFileSystem::Impl::AddPathAlias(
        const std::string_view alias, const std::string_view target) {
        const auto normalized_alias = NormalizeAbsolutePath(alias, true);
        const auto normalized_target = NormalizeAbsolutePath(target, true);
        if (normalized_alias == "/" || normalized_alias == normalized_target) {
            throw VfsError(kEinval, "VFS path alias is invalid");
        }
        std::scoped_lock lock(mutex_);
        if (aliases_.contains(normalized_alias)) {
            throw VfsError(kEexist, "VFS path alias already exists");
        }
        aliases_.emplace(normalized_alias, normalized_target);
    }

std::optional<std::string> VirtualFileSystem::Impl::WorkingDirectory() const {
        std::scoped_lock lock(mutex_);
        return working_directory_;
    }

std::string VirtualFileSystem::Impl::CanonicalPath(
        const std::string_view path) const {
        std::scoped_lock lock(mutex_);
        return ResolvePath(path, working_directory_, aliases_);
    }

std::int32_t VirtualFileSystem::Impl::Open(const std::string_view path,
                                    const VfsOpenOptions options) {
        if (!options.read && !options.write) {
            throw VfsError(kEinval, "VFS open has no access mode");
        }
        if (options.directory) {
            if (options.write || options.create || options.truncate) {
                throw VfsError(kEinval,
                               "VFS directory open has file-only options");
            }
            return OpenDirectory(path);
        }
        std::unique_lock lock(mutex_);
        const auto normalized = ResolvePath(path, working_directory_, aliases_);
        auto found = files_.find(normalized);
        if (found == files_.end()) {
            if (!options.create) throw VfsError(kEnoent, "VFS file not found");
            if (IsDirectoryLocked(normalized)) {
                throw VfsError(kEisdir, "VFS path is a directory");
            }
            if (sandbox_ != nullptr &&
                !IsWritableNamespaceLocked(normalized)) {
                throw VfsError(kEacces,
                               "VFS path is outside the writable namespace");
            }
            found = files_.emplace(
                normalized,
                    std::make_shared<File>(
                    File{{}, 0, {}, {}, true, VfsSource::runtime,
                          false, {}})).first;
            found->second->node_id = next_node_id_++;
            // Creation makes the path visible immediately. The host-side
            // tombstone is cleared at the next flush, but it must stop
            // shadowing the new in-memory node in this session now.
            tombstones_.erase(normalized);
            MarkOverlayLocked(normalized, *found->second);
            SetNodeSizeDirtyLocked(*found->second, 0,
                                   !found->second->overlay_path.empty());
        }
        if (options.write && !found->second->writable) {
            throw VfsError(kEacces, "VFS file is read-only");
        }
        if (options.write) {
            if (sandbox_ != nullptr &&
                !IsWritableNamespaceLocked(normalized)) {
                throw VfsError(kEacces,
                               "VFS path is outside the writable namespace");
            }
            MarkOverlayLocked(normalized, *found->second);
        }
        auto selected_file = found->second;
        if (options.truncate) {
            if (!options.write) {
                throw VfsError(kEinval, "VFS truncate requires write access");
            }
            auto file = selected_file;
            lock.unlock();
            std::scoped_lock file_lock(*file->mutex);
            file->contents.clear();
            file->read_all = {};
            file->read_at = {};
            lock.lock();
            ++file->generation;
            SetNodeSizeDirtyLocked(*file, 0,
                                   file->dirty || !file->overlay_path.empty());
        }
        const auto descriptor = AllocateDescriptor();
        descriptors_.emplace(descriptor, std::make_shared<OpenFile>(
            std::move(selected_file), 0, options.read, options.write, nullptr));
        return descriptor;
    }

VfsPipeDescriptors VirtualFileSystem::Impl::CreatePipe() {
        std::scoped_lock lock(mutex_);
        auto pipe = std::make_shared<File>(
            File{{}, 0, {}, {}, true, VfsSource::runtime, false, {}});
        pipe->node_id = next_node_id_++;
        const auto read_descriptor = AllocateDescriptor();
        descriptors_.emplace(read_descriptor, std::make_shared<OpenFile>(
            pipe, 0, true, false, nullptr, true));
        const auto write_descriptor = AllocateDescriptor();
        descriptors_.emplace(write_descriptor, std::make_shared<OpenFile>(
            std::move(pipe), 0, false, true, nullptr, true));
        return {read_descriptor, write_descriptor};
    }

std::size_t VirtualFileSystem::Impl::Read(const std::int32_t descriptor,
                                   const std::span<std::byte> destination) {
        std::shared_ptr<OpenFile> open;
        {
            std::scoped_lock lock(mutex_);
            open = FindDescriptor(descriptor);
        }
        std::scoped_lock operation(open->mutex);
        if (open->directory) throw VfsError(kEisdir, "VFS descriptor is a directory");
        if (!open->readable) throw VfsError(kEbadf, "VFS descriptor is not readable");
        auto file = open->file;
        std::scoped_lock file_lock(*file->mutex);
        const auto available = open->offset >= file->size ? 0 : file->size - open->offset;
        const auto count = static_cast<std::size_t>(
            std::min<std::uint64_t>(destination.size(), available));
        if (count != 0) {
            auto bounded = destination.first(count);
            if (file->read_at) {
                try {
                    const auto got = file->read_at(open->offset, bounded);
                    backing_read_bytes_.fetch_add(got, std::memory_order_relaxed);
                    if (got != count) throw VfsError(kEio, "VFS backing was truncated");
                } catch (const VfsError&) {
                    throw;
                } catch (const std::exception& error) {
                    throw VfsError(kEio, std::string("VFS backing read failed: ") +
                                             error.what());
                }
            } else {
                Materialize(*file);
                using Difference = std::vector<std::byte>::difference_type;
                std::copy_n(file->contents.begin() +
                                static_cast<Difference>(open->offset),
                            count, destination.begin());
            }
        }
        open->offset += count;
        return count;
    }

std::size_t VirtualFileSystem::Impl::ReadAt(
        const std::int32_t descriptor, const std::uint64_t offset,
        const std::span<std::byte> destination) {
        std::shared_ptr<OpenFile> open;
        {
            std::scoped_lock lock(mutex_);
            open = FindDescriptor(descriptor);
        }
        std::scoped_lock operation(open->mutex);
        if (open->directory) throw VfsError(kEisdir, "VFS descriptor is a directory");
        if (open->pipe) throw VfsError(kEspipe, "VFS pipe does not support positioned IO");
        if (!open->readable) throw VfsError(kEbadf, "VFS descriptor is not readable");
        auto file = open->file;
        std::scoped_lock file_lock(*file->mutex);
        const auto available = offset >= file->size ? 0 : file->size - offset;
        const auto count = static_cast<std::size_t>(
            std::min<std::uint64_t>(destination.size(), available));
        if (count == 0) return 0;
        auto bounded = destination.first(count);
        if (file->read_at) {
            try {
                const auto got = file->read_at(offset, bounded);
                backing_read_bytes_.fetch_add(got, std::memory_order_relaxed);
                if (got != count) {
                    throw VfsError(kEio, "VFS backing was truncated");
                }
                return got;
            } catch (const VfsError&) {
                throw;
            } catch (const std::exception& error) {
                throw VfsError(kEio, std::string("VFS backing read failed: ") +
                                         error.what());
            }
        }
        Materialize(*file);
        using Difference = std::vector<std::byte>::difference_type;
        std::copy_n(file->contents.begin() + static_cast<Difference>(offset),
                    count, destination.begin());
        return count;
    }

std::size_t VirtualFileSystem::Impl::Write(
        const std::int32_t descriptor,
        const std::span<const std::byte> source) {
        std::shared_ptr<OpenFile> open;
        {
            std::scoped_lock lock(mutex_);
            open = FindDescriptor(descriptor);
        }
        std::scoped_lock operation(open->mutex);
        if (open->directory) throw VfsError(kEisdir, "VFS descriptor is a directory");
        if (!open->writable) throw VfsError(kEbadf, "VFS descriptor is not writable");
        auto file = open->file;
        std::scoped_lock file_lock(*file->mutex);
        Materialize(*file);
        const auto end = open->offset + source.size();
        if (end > std::numeric_limits<std::size_t>::max()) {
            throw VfsError(kEfbig, "VFS file size is not representable");
        }
        {
            std::scoped_lock lock(mutex_);
            RequireSandboxQuotaLocked(*file,
                                      std::max<std::uint64_t>(file->size, end));
            std::shared_ptr<const VfsResourceReservation> growth;
            if (!file->materialized_reservations.empty() &&
                end > file->materialized_reserved_bytes) {
                growth = ReserveResourceMemory(
                    end - file->materialized_reserved_bytes, false);
            }
            if (growth) {
                file->materialized_reservations.push_back(std::move(growth));
                file->materialized_reserved_bytes = end;
            }
            if (end > file->contents.size()) {
                file->contents.resize(static_cast<std::size_t>(end));
            }
            using Difference = std::vector<std::byte>::difference_type;
            std::copy(source.begin(), source.end(),
                      file->contents.begin() +
                          static_cast<Difference>(open->offset));
            ++file->generation;
            open->offset = end;
            SetNodeSizeDirtyLocked(*file, file->contents.size(),
                                   file->dirty || !file->overlay_path.empty());
        }
        return source.size();
    }

std::size_t VirtualFileSystem::Impl::WriteAt(
        const std::int32_t descriptor, const std::uint64_t offset,
        const std::span<const std::byte> source) {
        std::shared_ptr<OpenFile> open;
        {
            std::scoped_lock lock(mutex_);
            open = FindDescriptor(descriptor);
        }
        std::scoped_lock operation(open->mutex);
        if (open->directory) throw VfsError(kEisdir, "VFS descriptor is a directory");
        if (open->pipe) throw VfsError(kEspipe, "VFS pipe does not support positioned IO");
        if (!open->writable) throw VfsError(kEbadf, "VFS descriptor is not writable");
        auto file = open->file;
        std::scoped_lock file_lock(*file->mutex);
        Materialize(*file);
        if (source.size() > std::numeric_limits<std::uint64_t>::max() - offset) {
            throw VfsError(kEfbig, "VFS positioned write overflows");
        }
        const auto end = offset + source.size();
        if (end > std::numeric_limits<std::size_t>::max()) {
            throw VfsError(kEfbig, "VFS file size is not representable");
        }
        {
            std::scoped_lock lock(mutex_);
            RequireSandboxQuotaLocked(*file,
                                      std::max<std::uint64_t>(file->size, end));
            std::shared_ptr<const VfsResourceReservation> growth;
            if (!file->materialized_reservations.empty() &&
                end > file->materialized_reserved_bytes) {
                growth = ReserveResourceMemory(
                    end - file->materialized_reserved_bytes, false);
            }
            if (growth) {
                file->materialized_reservations.push_back(std::move(growth));
                file->materialized_reserved_bytes = end;
            }
            if (end > file->contents.size()) {
                file->contents.resize(static_cast<std::size_t>(end));
            }
            using Difference = std::vector<std::byte>::difference_type;
            std::copy(source.begin(), source.end(),
                      file->contents.begin() + static_cast<Difference>(offset));
            ++file->generation;
            SetNodeSizeDirtyLocked(*file, file->contents.size(),
                                   file->dirty || !file->overlay_path.empty());
        }
        return source.size();
    }

std::shared_ptr<const VfsReadLease> VirtualFileSystem::Impl::CaptureReadLease(
        const std::int32_t descriptor, const std::uint64_t offset,
        const std::uint64_t requested_length) {
    std::shared_ptr<OpenFile> open;
    {
        std::scoped_lock lock(mutex_);
        open = FindDescriptor(descriptor);
    }
    std::scoped_lock operation(open->mutex);
    if (open->directory) throw VfsError(kEisdir, "cannot lease a directory");
    if (open->pipe) throw VfsError(kEspipe, "cannot lease a VFS pipe");
    if (!open->readable) throw VfsError(kEbadf, "VFS descriptor is not readable");
    auto file = open->file;
    std::scoped_lock file_lock(*file->mutex);
    if (offset > file->size) throw VfsError(kEinval, "VFS lease offset is past EOF");
    const auto available = file->size - offset;
    const auto length = requested_length == std::numeric_limits<std::uint64_t>::max()
                            ? available
                            : requested_length;
    if (length > available) throw VfsError(kEinval, "VFS lease exceeds EOF");
    if (!file->writable) {
        // A read_at backing is a stable owned source and can be shared. A
        // legacy read_all-only backing has no positioned lifetime contract,
        // so freeze it once at capture and account the materialization.
        if (!file->read_at && file->read_all) Materialize(*file);
        return std::make_shared<BackingLease>(file, offset, length);
    }
    if (length > std::numeric_limits<std::size_t>::max())
        throw VfsError(kEfbig, "VFS writable lease is too large");
    auto reservation = ReserveResourceMemory(length, true);
    Materialize(*file);
    std::vector<std::byte> snapshot(static_cast<std::size_t>(length));
    using Difference = std::vector<std::byte>::difference_type;
    std::copy_n(file->contents.begin() + static_cast<Difference>(offset),
                snapshot.size(), snapshot.begin());
    return std::make_shared<SnapshotLease>(std::move(snapshot),
                                           std::move(reservation));
}

std::uint64_t VirtualFileSystem::Impl::Seek(const std::int32_t descriptor,
                                     const std::int64_t offset,
                                     const VfsSeekWhence whence) {
        std::shared_ptr<OpenFile> open;
        {
            std::scoped_lock lock(mutex_);
            open = FindDescriptor(descriptor);
        }
        std::scoped_lock operation(open->mutex);
        if (open->directory) {
            std::uint64_t base{};
            if (whence == VfsSeekWhence::current) {
                base = open->directory->cursor;
            } else if (whence == VfsSeekWhence::end) {
                base = open->directory->entries.size();
            }
            std::uint64_t result{};
            if (offset < 0) {
                const auto magnitude =
                    static_cast<std::uint64_t>(-(offset + 1)) + 1U;
                if (magnitude > base) {
                    throw VfsError(kEinval, "negative VFS directory seek");
                }
                result = base - magnitude;
            } else {
                result = base + static_cast<std::uint64_t>(offset);
                if (result > open->directory->entries.size()) {
                    throw VfsError(kEinval, "VFS directory seek is past end");
                }
            }
            open->directory->cursor = static_cast<std::size_t>(result);
            return result;
        }
        if (open->pipe) throw VfsError(kEspipe, "VFS pipe is not seekable");
        std::uint64_t base{};
        if (whence == VfsSeekWhence::current) base = open->offset;
        if (whence == VfsSeekWhence::end) {
            std::scoped_lock file_lock(*open->file->mutex);
            base = open->file->size;
        }
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
        open->offset = result;
        return result;
    }

void VirtualFileSystem::Impl::Close(const std::int32_t descriptor) {
        std::shared_ptr<OpenFile> open;
        {
            std::scoped_lock lock(mutex_);
            const auto found = descriptors_.find(descriptor);
            if (found == descriptors_.end()) {
                throw VfsError(kEbadf, "VFS descriptor is not open");
            }
            open = found->second;
            descriptors_.erase(found);
        }
        std::scoped_lock operation(open->mutex);
        if (!open->file) return;
        {
            std::scoped_lock lock(mutex_);
            if (!open->writable && !open->file->dirty) return;
        }
        std::scoped_lock file_lock(*open->file->mutex);
        std::scoped_lock lock(mutex_);
        FlushFileLocked(*open->file);
    }

void VirtualFileSystem::Impl::Materialize(File& file) {
        if (!file.read_all) return;
        std::shared_ptr<const VfsResourceReservation> reservation;
        if (file.writable) {
            reservation = ReserveResourceMemory(file.size, false);
        }
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
        if (reservation) {
            file.materialized_reservations.push_back(std::move(reservation));
            file.materialized_reserved_bytes = file.contents.size();
        }
        full_materialized_bytes_.fetch_add(file.contents.size(),
                                           std::memory_order_relaxed);
        file.read_all = {};
        file.read_at = {};
    }

std::int32_t VirtualFileSystem::Impl::AllocateDescriptor() const {
        for (std::int32_t descriptor = 3;
             descriptor < std::numeric_limits<std::int32_t>::max();
             ++descriptor) {
            if (!descriptors_.contains(descriptor)) return descriptor;
        }
        throw VfsError(kEbadf, "VFS descriptor table is full");
    }

std::shared_ptr<OpenFile> VirtualFileSystem::Impl::FindDescriptor(
        const std::int32_t descriptor) const {
        const auto found = descriptors_.find(descriptor);
        if (found == descriptors_.end()) {
            throw VfsError(kEbadf, "VFS descriptor is not open");
        }
        return found->second;
    }


VirtualFileSystem::VirtualFileSystem(const VfsConfig config)
    : impl_(std::make_unique<Impl>(config)) {}
VirtualFileSystem::~VirtualFileSystem() = default;

VfsResourceReservation::VfsResourceReservation(
    std::function<void()> release)
    : release_(std::move(release)) {}

VfsResourceReservation::~VfsResourceReservation() {
    if (release_) release_();
}

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

std::shared_ptr<const VfsResourceReservation>
VirtualFileSystem::ReserveResourceMemory(const std::uint64_t bytes) {
    return impl_->ReserveResourceMemory(bytes, false);
}

void VirtualFileSystem::AddPathAlias(
    const std::string_view alias, const std::string_view target) {
    impl_->AddPathAlias(alias, target);
}
void VirtualFileSystem::SetWorkingDirectory(const std::string_view path) {
    impl_->SetWorkingDirectory(path);
}
std::optional<std::string> VirtualFileSystem::WorkingDirectory() const {
    return impl_->WorkingDirectory();
}
std::string VirtualFileSystem::CanonicalPath(const std::string_view path) const {
    return impl_->CanonicalPath(path);
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
std::int32_t VirtualFileSystem::OpenDirectory(const std::string_view path) {
    return impl_->OpenDirectory(path);
}
std::vector<VfsDirectoryEntry> VirtualFileSystem::ReadDirectory(
    const std::int32_t descriptor, const std::size_t maximum) {
    return impl_->ReadDirectory(descriptor, maximum);
}
VfsFileInfo VirtualFileSystem::DescriptorInfo(
    const std::int32_t descriptor) const {
    return impl_->DescriptorInfo(descriptor);
}
VfsPipeDescriptors VirtualFileSystem::CreatePipe() {
    return impl_->CreatePipe();
}
std::size_t VirtualFileSystem::Read(const std::int32_t descriptor,
                                    const std::span<std::byte> destination) {
    return impl_->Read(descriptor, destination);
}
std::size_t VirtualFileSystem::ReadAt(
    const std::int32_t descriptor, const std::uint64_t offset,
    const std::span<std::byte> destination) {
    return impl_->ReadAt(descriptor, offset, destination);
}
std::size_t VirtualFileSystem::Write(
    const std::int32_t descriptor,
    const std::span<const std::byte> source) {
    return impl_->Write(descriptor, source);
}
std::size_t VirtualFileSystem::WriteAt(
    const std::int32_t descriptor, const std::uint64_t offset,
    const std::span<const std::byte> source) {
    return impl_->WriteAt(descriptor, offset, source);
}
std::shared_ptr<const VfsReadLease> VirtualFileSystem::CaptureReadLease(
    const std::int32_t descriptor, const std::uint64_t offset,
    const std::uint64_t length) {
    return impl_->CaptureReadLease(descriptor, offset, length);
}

VfsIoStatistics VirtualFileSystem::IoStatistics() const {
    return impl_->IoStatistics();
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
void VirtualFileSystem::AttachSandbox(
    SandboxStore& store, const std::span<const std::string> writable_roots) {
    impl_->AttachSandbox(store, writable_roots);
}
bool VirtualFileSystem::SandboxAttached() const {
    return impl_->SandboxAttached();
}
void VirtualFileSystem::Close(const std::int32_t descriptor) {
    impl_->Close(descriptor);
}

}  // namespace ogplay::runtime
