#include "external_data.h"

#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "ogplay/hal/fs.h"

namespace ogplay::app {
namespace {

constexpr unsigned kMaximumStagingSlots = 1024;
constexpr std::size_t kCopyChunkBytes = 1U << 20U;

struct DataItem final {
    std::filesystem::path relative;
    bool directory{};
    std::uint64_t size{};
};

[[nodiscard]] bool Contains(const std::filesystem::path& parent,
                            const std::filesystem::path& child) {
    const auto relative = child.lexically_relative(parent);
    return !relative.empty() && *relative.begin() != "..";
}

[[nodiscard]] std::vector<DataItem> ScanDataPack(const std::filesystem::path& source,
                                                 std::uint64_t& total) {
    std::vector<DataItem> items;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(source, error), end;
         !error && iterator != end; iterator.increment(error)) {
        const auto status = iterator->symlink_status(error);
        if (error) break;
        auto relative = iterator->path().lexically_relative(source);
        if (std::filesystem::is_directory(status)) {
            items.push_back({std::move(relative), true, 0});
        } else if (std::filesystem::is_regular_file(status)) {
            const auto size = iterator->file_size(error);
            if (error) break;
            items.push_back({std::move(relative), false, size});
            total += size;
        } else {
            // Following links could copy data outside the selected directory.
            throw LibraryError(LibraryErrorCode::invalid_argument,
                                "external data contains a symbolic link or special file; "
                                "link the directory instead of copying it",
                                iterator->path());
        }
    }
    if (error) {
        throw LibraryError(LibraryErrorCode::io_error,
                            "cannot scan external data directory: " + error.message(), source);
    }
    return items;
}

void AddCopied(ImportProgress* progress, const std::uint64_t bytes) {
    if (progress != nullptr) progress->copied_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void StreamCopy(const std::filesystem::path& from, const std::filesystem::path& to,
                const std::uint64_t expected, ImportProgress* progress) {
    std::ifstream input(from, std::ios::binary);
    if (!input) throw LibraryError(LibraryErrorCode::io_error, "cannot open external data file", from);
    std::ofstream output(to, std::ios::binary | std::ios::trunc);
    if (!output) throw LibraryError(LibraryErrorCode::io_error, "cannot create library data file", to);
    std::vector<char> buffer(kCopyChunkBytes);
    std::uint64_t copied = 0;
    for (;;) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            output.write(buffer.data(), count);
            if (!output) throw LibraryError(LibraryErrorCode::io_error, "cannot write library data file", to);
            copied += static_cast<std::uint64_t>(count);
            AddCopied(progress, static_cast<std::uint64_t>(count));
            ThrowIfImportCancelled(progress);
        }
        if (!input) {
            if (input.eof()) break;
            throw LibraryError(LibraryErrorCode::io_error, "cannot read external data file", from);
        }
    }
    output.close();
    if (!output) throw LibraryError(LibraryErrorCode::io_error, "cannot finish library data file", to);
    if (copied != expected) {
        throw LibraryError(LibraryErrorCode::io_error,
                            "external data file changed while it was copied", from);
    }
}

void CopyDataPack(const std::filesystem::path& source, const std::filesystem::path& destination,
                  ImportProgress* progress) {
    std::uint64_t total = 0;
    const auto items = ScanDataPack(source, total);
    if (progress != nullptr) progress->total_bytes.store(total, std::memory_order_relaxed);
    std::uint64_t done = 0;
    bool space_checked = false;
    for (const auto& item : items) {
        ThrowIfImportCancelled(progress);
        const auto target = destination / item.relative;
        if (item.directory) {
            std::filesystem::create_directory(target);
            continue;
        }
        if (hal::TryCloneFile(source / item.relative, target)) {
            AddCopied(progress, item.size);
        } else {
            // Clones share extents, so space only matters once a real copy is needed.
            if (!space_checked) {
                const auto available = std::filesystem::space(destination).available;
                if (available < total - done) {
                    throw LibraryError(LibraryErrorCode::io_error,
                                        "not enough free space to copy external data: need " +
                                            std::to_string(total - done) + " bytes, " +
                                            std::to_string(available) + " available",
                                        destination);
                }
                space_checked = true;
            }
            StreamCopy(source / item.relative, target, item.size, progress);
        }
        done += item.size;
    }
}

void RemoveIfPresent(const std::filesystem::path& path) {
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(path, error));
    if (error) throw std::filesystem::filesystem_error("remove_all", path, error);
}

// Directories whose lock nobody holds belong to an interrupted import.
void SweepStaleStaging(const std::filesystem::path& parent) {
    std::error_code error;
    std::vector<std::filesystem::path> directories;
    for (std::filesystem::directory_iterator iterator(parent, error), end;
         !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_directory(error)) directories.push_back(iterator->path());
    }
    for (const auto& directory : directories) {
        auto lock_path = directory;
        lock_path += ".lock";
        if (const auto lock = hal::FileLock::TryAcquire(lock_path)) RemoveIfPresent(directory);
    }
}

}  // namespace

void ThrowImportCancelled() {
    throw LibraryError(LibraryErrorCode::io_error,
                        "import_cancelled: the import was cancelled before publication");
}

void ThrowIfImportCancelled(const ImportProgress* progress) {
    if (progress != nullptr && progress->cancel_requested.load(std::memory_order_relaxed)) {
        ThrowImportCancelled();
    }
}

StagedExternalData::StagedExternalData(const std::filesystem::path& library_root,
                                       const std::filesystem::path& source,
                                       ImportProgress* progress) {
    std::error_code error;
    const auto canonical_source = std::filesystem::weakly_canonical(source, error);
    const auto canonical_root = error ? std::filesystem::path{} :
        std::filesystem::weakly_canonical(library_root, error);
    if (error || !std::filesystem::is_directory(canonical_source, error) || error) {
        throw LibraryError(LibraryErrorCode::not_found,
                            "selected external data directory is unavailable", source);
    }
    if (canonical_source == canonical_root || Contains(canonical_source, canonical_root) ||
        Contains(canonical_root, canonical_source)) {
        throw LibraryError(LibraryErrorCode::invalid_argument,
                            "external data directory overlaps the game library", source);
    }
    const auto parent = library_root / "import-staging";
    try {
        std::filesystem::create_directories(parent);
        SweepStaleStaging(parent);
        for (unsigned slot = 0; slot < kMaximumStagingSlots && !lock_; ++slot) {
            const auto directory = parent / ("data-" + std::to_string(slot));
            auto lock_path = directory;
            lock_path += ".lock";
            auto lock = hal::FileLock::TryAcquire(lock_path);
            if (!lock) continue;
            RemoveIfPresent(directory);
            std::filesystem::create_directory(directory);
            lock_ = std::move(lock);
            directory_ = directory;
        }
    } catch (const LibraryError&) {
        throw;
    } catch (const std::exception& exception) {
        throw LibraryError(LibraryErrorCode::io_error,
                            std::string("cannot prepare data staging: ") + exception.what(), parent);
    }
    if (!lock_) throw LibraryError(LibraryErrorCode::busy, "too many concurrent data imports", parent);
    try {
        CopyDataPack(canonical_source, directory_, progress);
    } catch (...) {
        // A throwing constructor skips the destructor; drop the partial copy here.
        std::error_code cleanup;
        static_cast<void>(std::filesystem::remove_all(directory_, cleanup));
        try {
            throw;
        } catch (const LibraryError&) {
            throw;
        } catch (const std::exception& exception) {
            throw LibraryError(LibraryErrorCode::io_error,
                                std::string("cannot copy external data: ") + exception.what(), source);
        }
    }
}

StagedExternalData::~StagedExternalData() {
    if (directory_.empty()) return;
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(directory_, error));
}

void StagedExternalData::Publish(const std::filesystem::path& destination) {
    std::filesystem::rename(directory_, destination);
    directory_.clear();
}

}  // namespace ogplay::app
