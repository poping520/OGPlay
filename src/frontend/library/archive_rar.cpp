#include "archive_source.h"
#include "external_data.h"

#include "ogplay/hal/fs.h"
#include "ogplay/hal/utf8_locale.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <vector>

namespace ogplay::app {
namespace {

constexpr std::size_t kReadBytes = 1U << 20U;

// libarchive reads the archive through the host file (so an fd handed over by Android SAF works as well), one
// sequential pass per listing or extraction; RAR4 and RAR5 only.
class Reader final {
public:
    explicit Reader(std::shared_ptr<const hal::HostReadFile> file) : file_(std::move(file)), buffer_(kReadBytes) {
        handle_ = archive_read_new();
        if (!handle_) throw std::bad_alloc();
        archive_read_support_format_rar(handle_);
        archive_read_support_format_rar5(handle_);
        archive_read_set_read_callback(handle_, &Reader::ReadCallback);
        archive_read_set_seek_callback(handle_, &Reader::SeekCallback);
        archive_read_set_skip_callback(handle_, &Reader::SkipCallback);
        archive_read_set_callback_data(handle_, this);
        if (archive_read_open1(handle_) != ARCHIVE_OK) Fail();
    }
    ~Reader() { archive_read_free(handle_); }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    // The next member, or null at the end.
    archive_entry* Next() {
        archive_entry* entry{};
        const auto status = archive_read_next_header(handle_, &entry);
        if (status == ARCHIVE_EOF) return nullptr;
        if (status < ARCHIVE_WARN) Fail();
        return entry;
    }
    std::size_t Read(std::span<std::byte> output) {
        const auto count = archive_read_data(handle_, output.data(), output.size());
        if (count < 0) Fail();
        return static_cast<std::size_t>(count);
    }
    void Skip() {
        if (archive_read_data_skip(handle_) < ARCHIVE_WARN) Fail();
    }
    [[noreturn]] void Fail() const {
        const char* message = archive_error_string(handle_);
        throw LibraryError(LibraryErrorCode::invalid_argument,
                           std::string("RAR 压缩包无法读取：") + (message ? message : "未知错误"));
    }

private:
    static la_ssize_t ReadCallback(archive*, void* data, const void** buffer) {
        auto& self = *static_cast<Reader*>(data);
        try {
            const auto count = self.file_->ReadAt(self.offset_, self.buffer_);
            self.offset_ += count;
            *buffer = self.buffer_.data();
            return static_cast<la_ssize_t>(count);
        } catch (const std::exception&) {
            return -1;
        }
    }
    static la_int64_t SeekCallback(archive*, void* data, la_int64_t offset, int whence) {
        auto& self = *static_cast<Reader*>(data);
        const auto size = static_cast<la_int64_t>(self.file_->Info().size);
        const la_int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? static_cast<la_int64_t>(self.offset_) : size;
        const auto target = base + offset;
        if (target < 0) return ARCHIVE_FATAL;
        self.offset_ = static_cast<std::uint64_t>(target);
        return target;
    }
    static la_int64_t SkipCallback(archive*, void* data, la_int64_t request) {
        auto& self = *static_cast<Reader*>(data);
        const auto size = self.file_->Info().size;
        const auto available = size > self.offset_ ? size - self.offset_ : 0;
        const auto count = std::min<std::uint64_t>(static_cast<std::uint64_t>(std::max<la_int64_t>(request, 0)), available);
        self.offset_ += count;
        return static_cast<la_int64_t>(count);
    }

    hal::ScopedUtf8Locale locale_;   // member names come out of libarchive in the thread's charset
    std::shared_ptr<const hal::HostReadFile> file_;
    std::vector<std::byte> buffer_;
    std::uint64_t offset_{};
    archive* handle_{};
};

[[nodiscard]] std::string MemberName(archive_entry* entry) {
    if (const char* utf8 = archive_entry_pathname_utf8(entry)) return utf8;
    if (const char* raw = archive_entry_pathname(entry)) return DecodeArchiveName(raw);
    return {};
}

class RarSource final : public ImportSource {
public:
    explicit RarSource(const std::filesystem::path& path) {
        try {
            file_ = hal::OpenHostReadFile(path);
        } catch (const std::exception&) {
            throw LibraryError(LibraryErrorCode::not_found, "无法打开所选文件。", path);
        }
        Reader reader(file_);
        for (std::size_t ordinal = 0;; ++ordinal) {
            auto* entry = reader.Next();
            if (!entry) break;
            if (archive_entry_is_encrypted(entry))
                throw LibraryError(LibraryErrorCode::invalid_argument, "暂不支持加密的压缩包，请先解压。");
            if (archive_entry_filetype(entry) != AE_IFREG) continue;
            const auto name = MemberName(entry);
            const auto normalized = NormalizeArchivePath(name);
            if (!normalized) throw LibraryError(LibraryErrorCode::invalid_argument, "压缩包含不安全的路径：" + name);
            if (normalized->empty()) continue;
            files_.push_back({*normalized, static_cast<std::uint64_t>(std::max<la_int64_t>(archive_entry_size(entry), 0))});
            ordinals_.push_back(ordinal);
        }
    }
    const std::vector<ImportFile>& Files() const override { return files_; }

    // Solid archives decompress everything up to the last wanted member; others skip by seeking.
    void Extract(std::span<const ImportExtraction> files, ImportProgress* progress) override {
        std::map<std::size_t, const ImportExtraction*> wanted;
        for (const auto& item : files) wanted.emplace(ordinals_.at(item.index), &item);
        Reader reader(file_);
        for (std::size_t ordinal = 0; !wanted.empty(); ++ordinal) {
            auto* entry = reader.Next();
            if (!entry) throw LibraryError(LibraryErrorCode::invalid_argument, "压缩包内容不完整。");
            const auto found = wanted.find(ordinal);
            if (found == wanted.end()) {
                ThrowIfImportCancelled(progress);
                reader.Skip();
                continue;
            }
            const auto& item = *found->second;
            WriteImportFile(item.destination, files_[item.index].size,
                            [&](std::span<std::byte> buffer) { return reader.Read(buffer); }, progress);
            wanted.erase(found);
        }
    }

private:
    std::shared_ptr<const hal::HostReadFile> file_;
    std::vector<ImportFile> files_;
    std::vector<std::size_t> ordinals_;
};

}  // namespace

std::unique_ptr<ImportSource> OpenRarArchive(const std::filesystem::path& path) {
    return std::make_unique<RarSource>(path);
}

}  // namespace ogplay::app
