#include "ogplay/app/archive_import.h"

#include "archive7z.h"
#include "external_data.h"
#include "ogplay/hal/fs.h"

#include <exception>
#include <map>
#include <utility>

namespace ogplay::app {
namespace {

[[noreturn]] void RejectArchive(const ogplay_7z_error& error) {
    switch (error.code) {
    case OGPLAY_7Z_CANCELLED: ThrowImportCancelled();
    case OGPLAY_7Z_ENCRYPTED:
        throw LibraryError(LibraryErrorCode::invalid_argument, "encrypted archives are not supported",
                           UiMessage{"host.error.archiveEncrypted"});
    case OGPLAY_7Z_MULTIVOLUME:
        throw LibraryError(LibraryErrorCode::invalid_argument, "multi-volume archives are not supported",
                           UiMessage{"host.error.archiveMultiVolume"});
    case OGPLAY_7Z_UNSUPPORTED:
        throw LibraryError(LibraryErrorCode::invalid_argument,
                           "unsupported archive compression method: " + std::string(error.message),
                           UiMessage{"host.error.archiveMethod", {{"detail", std::string(error.message)}}});
    case OGPLAY_7Z_IO:
    case OGPLAY_7Z_NO_MEMORY: throw LibraryError(LibraryErrorCode::io_error, error.message);
    default:
        throw LibraryError(LibraryErrorCode::invalid_argument,
                           "cannot decode the archive: " + std::string(error.message),
                           UiMessage{"host.error.archiveDecode", {{"detail", std::string(error.message)}}});
    }
}

struct ArchiveReader {
    std::shared_ptr<const hal::HostReadFile> file;
    std::exception_ptr failure;

    static int Read(void* context, uint64_t offset, void* data, size_t capacity, size_t* count) noexcept {
        auto& self = *static_cast<ArchiveReader*>(context);
        try {
            *count = self.file->ReadAt(offset, {static_cast<std::byte*>(data), capacity});
            return 0;
        } catch (...) {
            self.failure = std::current_exception();
            return 1;
        }
    }

    void Check(ogplay_7z_status status, const ogplay_7z_error& error) {
        if (failure) std::rethrow_exception(std::exchange(failure, {}));
        if (status != OGPLAY_7Z_OK) RejectArchive(error);
    }
};

struct Output {
    std::map<uint32_t, const ImportExtraction*> files;
    ImportProgress* progress{};
    std::exception_ptr failure;
    std::optional<ImportFileWriter> output;
    uint64_t reported{};

    template <class Operation>
    static int Run(void* context, Operation operation) noexcept {
        auto& self = *static_cast<Output*>(context);
        try {
            operation(self);
            return 0;
        } catch (...) {
            self.failure = std::current_exception();
            return 1;
        }
    }

    static int Begin(void* context, uint32_t index) noexcept {
        return Run(context, [=](Output& self) { self.output.emplace(self.files.at(index)->destination); });
    }

    static int Write(void* context, const void* data, size_t size) noexcept {
        return Run(context, [=](Output& self) { self.output->Write({static_cast<const std::byte*>(data), size}); });
    }

    static int End(void* context) noexcept {
        return Run(context, [](Output& self) {
            self.output->Finish();
            self.output.reset();
        });
    }

    static void Progress(void* context, uint64_t written) noexcept {
        auto& self = *static_cast<Output*>(context);
        if (self.progress) self.progress->copied_bytes.fetch_add(written - self.reported, std::memory_order_relaxed);
        self.reported = written;
    }

    static int Cancelled(void* context) noexcept {
        const auto* progress = static_cast<Output*>(context)->progress;
        return progress && progress->cancel_requested.load(std::memory_order_relaxed);
    }
};

class ArchiveSource final : public ImportSource {
public:
    explicit ArchiveSource(const std::filesystem::path& path) {
        try {
            reader_.file = hal::OpenHostReadFile(path);
        } catch (const std::exception&) {
            throw LibraryError(LibraryErrorCode::not_found, "cannot open the chosen file",
                               UiMessage{"host.error.openFile"}, path);
        }
        const ogplay_7z_input input{&reader_, reader_.file->Info().size, ArchiveReader::Read};
        ogplay_7z_error error{};
        ogplay_7z_archive* handle = nullptr;
        const auto status = ogplay_7z_open(&input, &handle, &error);
        handle_.reset(handle);
        reader_.Check(status, error);
        if (ogplay_7z_is_volume(handle)) RejectArchive({OGPLAY_7Z_MULTIVOLUME, {}});
        for (uint32_t i = 0; i < ogplay_7z_count(handle); ++i) {
            ogplay_7z_entry entry{};
            reader_.Check(ogplay_7z_get(handle, i, &entry, &error), error);
            if (entry.encrypted) RejectArchive({OGPLAY_7Z_ENCRYPTED, {}});
            if (entry.split) RejectArchive({OGPLAY_7Z_MULTIVOLUME, {}});
            const std::string_view raw(entry.name.bytes, entry.name.size);
            const auto name = entry.unicode_name.size ? std::string(entry.unicode_name.bytes, entry.unicode_name.size)
                              : entry.name_is_utf8    ? std::string(raw)
                                                      : DecodeArchiveName(raw);
            const auto normalized = NormalizeArchivePath(name);
            if (!normalized)
                throw LibraryError(LibraryErrorCode::invalid_argument,
                                   "unsafe archive path: " + DecodeArchiveName(name),
                                   UiMessage{"host.error.archiveUnsafePath", {{"path", DecodeArchiveName(name)}}});
            if (entry.directory || entry.special || name.ends_with('/') || name.ends_with('\\') || normalized->empty())
                continue;
            files_.push_back({*normalized, entry.size});
            indices_.push_back(i);
        }
    }

    const std::vector<ImportFile>& Files() const override { return files_; }

    void Extract(std::span<const ImportExtraction> files, ImportProgress* progress) override {
        ThrowIfImportCancelled(progress);
        Output state;
        state.progress = progress;
        for (const auto& item : files) {
            if (!state.files.emplace(indices_.at(item.index), &item).second)
                throw LibraryError(LibraryErrorCode::invalid_argument, "duplicate file in the archive",
                                   UiMessage{"host.error.archiveDuplicate"}, item.destination);
        }
        std::vector<uint32_t> indices;
        indices.reserve(state.files.size());
        for (const auto& [index, _] : state.files)
            indices.push_back(index);
        const ogplay_7z_output output{&state,      Output::Begin,    Output::Write,
                                      Output::End, Output::Progress, Output::Cancelled};
        ogplay_7z_error error{};
        const auto status =
            ogplay_7z_extract(handle_.get(), indices.data(), static_cast<uint32_t>(indices.size()), &output, &error);
        if (state.failure) std::rethrow_exception(state.failure);
        reader_.Check(status, error);
    }

private:
    ArchiveReader reader_;
    std::unique_ptr<ogplay_7z_archive, decltype(&ogplay_7z_close)> handle_{nullptr, ogplay_7z_close};
    std::vector<ImportFile> files_;
    std::vector<uint32_t> indices_;
};

}  // namespace

std::unique_ptr<ImportSource> OpenArchive(const std::filesystem::path& path) {
    return std::make_unique<ArchiveSource>(path);
}

}  // namespace ogplay::app
