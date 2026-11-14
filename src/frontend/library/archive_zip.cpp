#include "archive_source.h"
#include "external_data.h"

#include "ogplay/hal/fs.h"
#include "ogplay/loader/apk.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

namespace ogplay::app {
namespace {

constexpr std::uint32_t kLocalSignature = 0x04034b50, kCentralSignature = 0x02014b50, kEndSignature = 0x06054b50,
                        kLocatorSignature = 0x07064b50, kEnd64Signature = 0x06064b50;
constexpr std::size_t kEndSize = 22, kMaxComment = 0xFFFF;
constexpr std::uint64_t kMaxCentralDirectory = 256ULL << 20U;

[[nodiscard]] std::uint16_t U16(std::span<const std::byte> bytes, std::size_t at) {
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[at]) | std::to_integer<unsigned>(bytes[at + 1]) << 8U);
}
[[nodiscard]] std::uint32_t U32(std::span<const std::byte> bytes, std::size_t at) {
    return static_cast<std::uint32_t>(U16(bytes, at)) | static_cast<std::uint32_t>(U16(bytes, at + 2)) << 16U;
}
[[nodiscard]] std::uint64_t U64(std::span<const std::byte> bytes, std::size_t at) {
    return static_cast<std::uint64_t>(U32(bytes, at)) | static_cast<std::uint64_t>(U32(bytes, at + 4)) << 32U;
}

[[noreturn]] void Broken(std::string_view what) {
    throw LibraryError(LibraryErrorCode::invalid_argument, "压缩包已损坏或不是 ZIP 格式：" + std::string(what));
}

std::vector<std::byte> ReadExact(const hal::HostReadFile& file, std::uint64_t offset, std::size_t size) {
    std::vector<std::byte> bytes(size);
    if (offset > file.Info().size || size > file.Info().size - offset || file.ReadAt(offset, bytes) != size)
        Broken("内容越界");
    return bytes;
}

struct Member final {
    std::uint64_t local_offset{}, compressed{}, size{};
    std::uint16_t method{}, flags{};
    std::uint32_t crc{};
};
struct RawMember final {
    std::string name;   // as stored (bytes)
    bool utf8_flag{};
    std::optional<std::string> unicode_name;
    Member member;
};

std::vector<RawMember> ReadCentralDirectory(const hal::HostReadFile& file) {
    const auto size = file.Info().size;
    if (size < kEndSize) Broken("文件太短");
    const auto tail_size = static_cast<std::size_t>(std::min<std::uint64_t>(size, kEndSize + kMaxComment));
    const auto tail = ReadExact(file, size - tail_size, tail_size);
    std::optional<std::size_t> end;
    for (auto at = tail_size - kEndSize;; --at) {
        if (U32(tail, at) == kEndSignature && U16(tail, at + 20) <= tail_size - at - kEndSize) {
            end = at;
            break;
        }
        if (at == 0) break;
    }
    if (!end) Broken("找不到目录");
    const auto end_offset = size - tail_size + *end;
    if (U16(tail, *end + 4) != 0 || U16(tail, *end + 6) != 0)
        throw LibraryError(LibraryErrorCode::invalid_argument, "暂不支持分卷压缩包，请先合并或解压。");
    std::uint64_t count = U16(tail, *end + 10), directory_size = U32(tail, *end + 12),
                  directory_offset = U32(tail, *end + 16);
    if (count == 0xFFFF || directory_size == 0xFFFFFFFF || directory_offset == 0xFFFFFFFF) {
        if (end_offset < 20) Broken("ZIP64 定位记录缺失");
        const auto locator = ReadExact(file, end_offset - 20, 20);
        if (U32(locator, 0) != kLocatorSignature) Broken("ZIP64 定位记录缺失");
        const auto record = ReadExact(file, U64(locator, 8), 56);
        if (U32(record, 0) != kEnd64Signature) Broken("ZIP64 目录记录无效");
        count = U64(record, 32);
        directory_size = U64(record, 40);
        directory_offset = U64(record, 48);
    }
    if (directory_size > kMaxCentralDirectory) Broken("目录过大");
    const auto directory = ReadExact(file, directory_offset, static_cast<std::size_t>(directory_size));
    std::vector<RawMember> members;
    std::size_t at = 0;
    for (std::uint64_t index = 0; index < count; ++index) {
        if (at + 46 > directory.size() || U32(directory, at) != kCentralSignature) Broken("目录项无效");
        RawMember raw;
        auto& member = raw.member;
        member.flags = U16(directory, at + 8);
        member.method = U16(directory, at + 10);
        member.crc = U32(directory, at + 16);
        member.compressed = U32(directory, at + 20);
        member.size = U32(directory, at + 24);
        const std::size_t name_size = U16(directory, at + 28), extra_size = U16(directory, at + 30),
                          comment_size = U16(directory, at + 32);
        member.local_offset = U32(directory, at + 42);
        if (at + 46 + name_size + extra_size + comment_size > directory.size()) Broken("目录项越界");
        raw.name.assign(reinterpret_cast<const char*>(directory.data() + at + 46), name_size);
        raw.utf8_flag = (member.flags & 0x800U) != 0;
        const auto extra = std::span(directory).subspan(at + 46 + name_size, extra_size);
        for (std::size_t field = 0; field + 4 <= extra.size();) {
            const auto id = U16(extra, field), length = U16(extra, field + 2);
            if (field + 4 + length > extra.size()) break;
            const auto body = extra.subspan(field + 4, length);
            if (id == 0x0001) {   // ZIP64: only the fields whose 32-bit value saturated, in this order
                std::size_t next = 0;
                const auto take = [&](std::uint64_t& value) {
                    if (value == 0xFFFFFFFF && next + 8 <= body.size()) value = U64(body, next), next += 8;
                };
                take(member.size);
                take(member.compressed);
                take(member.local_offset);
            } else if (id == 0x7075 && length > 5 && std::to_integer<unsigned>(body[0]) == 1) {
                raw.unicode_name.emplace(reinterpret_cast<const char*>(body.data() + 5), body.size() - 5);
            }
            field += 4 + length;
        }
        members.push_back(std::move(raw));
        at += 46 + name_size + extra_size + comment_size;
    }
    return members;
}

class ZipSource final : public ImportSource {
public:
    explicit ZipSource(const std::filesystem::path& path) : file_(Open(path)) {
        for (auto& raw : ReadCentralDirectory(*file_)) {
            const auto name = raw.unicode_name ? *raw.unicode_name : raw.utf8_flag ? raw.name : DecodeArchiveName(raw.name);
            const auto normalized = NormalizeArchivePath(name);
            if (!normalized) throw LibraryError(LibraryErrorCode::invalid_argument, "压缩包含不安全的路径：" + name);
            const bool directory = name.ends_with('/') || name.ends_with('\\');
            if (directory || normalized->empty()) continue;
            if (raw.member.flags & 1U)
                throw LibraryError(LibraryErrorCode::invalid_argument, "暂不支持加密的压缩包，请先解压。");
            if (raw.member.method != 0 && raw.member.method != 8)
                throw LibraryError(LibraryErrorCode::invalid_argument, "压缩包使用了暂不支持的压缩方式：" + *normalized);
            files_.push_back({*normalized, raw.member.size});
            members_.push_back(raw.member);
        }
    }
    const std::vector<ImportFile>& Files() const override { return files_; }

    void Extract(std::span<const ImportExtraction> files, ImportProgress* progress) override {
        for (const auto& item : files) {
            const auto& member = members_.at(item.index);
            const auto header = ReadExact(*file_, member.local_offset, 30);
            if (U32(header, 0) != kLocalSignature) Broken("文件头无效");
            const auto data = member.local_offset + 30 + U16(header, 26) + U16(header, 28);
            if (member.method == 0) {
                if (member.compressed != member.size) Broken("存储项大小不一致");
                std::uint64_t at = data;
                WriteImportFile(item.destination, member.size, [&](std::span<std::byte> buffer) {
                    const auto count = file_->ReadAt(at, buffer);
                    at += count;
                    return count;
                }, progress);
                continue;
            }
            if (member.size > std::numeric_limits<std::uint32_t>::max() ||
                member.compressed > std::numeric_limits<std::uint32_t>::max())
                throw LibraryError(LibraryErrorCode::invalid_argument, "压缩包中的单个文件超过 4 GB，暂不支持。");
            if (!mapping_.owner) mapping_ = file_->Map();
            if (member.local_offset > mapping_.bytes.size()) Broken("内容越界");
            loader::ApkEntry entry;
            entry.compression_method = 8;
            entry.crc32 = member.crc;
            entry.compressed_size = static_cast<std::uint32_t>(member.compressed);
            entry.uncompressed_size = static_cast<std::uint32_t>(member.size);
            entry.local_header_offset = 0;
            std::optional<loader::ApkDeflateStream> stream;
            try {
                stream.emplace(mapping_.bytes.subspan(static_cast<std::size_t>(member.local_offset)), entry);
            } catch (const std::exception& error) {
                Broken(error.what());
            }
            std::size_t offset = 0;
            WriteImportFile(item.destination, member.size, [&](std::span<std::byte> buffer) {
                try {
                    const auto count = stream->Read(offset, buffer);
                    offset += count;
                    return count;
                } catch (const std::exception& error) {
                    Broken(error.what());
                }
            }, progress);
        }
    }

private:
    static std::shared_ptr<const hal::HostReadFile> Open(const std::filesystem::path& path) {
        try {
            return hal::OpenHostReadFile(path);
        } catch (const std::exception&) {
            throw LibraryError(LibraryErrorCode::not_found, "无法打开所选文件。", path);
        }
    }
    std::shared_ptr<const hal::HostReadFile> file_;
    hal::HostReadMapping mapping_;
    std::vector<ImportFile> files_;
    std::vector<Member> members_;
};

[[nodiscard]] std::array<std::byte, 8> Head(const std::filesystem::path& path) {
    std::array<std::byte, 8> head{};
    try {
        const auto file = hal::OpenHostReadFile(path);
        static_cast<void>(file->ReadAt(0, head));
    } catch (const std::exception&) {
        throw LibraryError(LibraryErrorCode::not_found, "无法打开所选文件。", path);
    }
    return head;
}
[[nodiscard]] bool Starts(const std::array<std::byte, 8>& head, std::string_view magic) {
    return std::memcmp(head.data(), magic.data(), magic.size()) == 0;
}
[[nodiscard]] bool IsZip(const std::array<std::byte, 8>& head) {
    return Starts(head, std::string_view("PK\x03\x04", 4)) || Starts(head, std::string_view("PK\x05\x06", 4));
}
[[nodiscard]] bool IsRar(const std::array<std::byte, 8>& head) {
    return Starts(head, std::string_view("Rar!\x1a\x07", 6));
}

}  // namespace

std::uint64_t ImportFileSize(const std::filesystem::path& source) {
    try {
        return hal::OpenHostReadFile(source)->Info().size;
    } catch (const std::exception&) {
        throw LibraryError(LibraryErrorCode::not_found, "无法打开所选文件。", source);
    }
}

void CopyImportFile(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::shared_ptr<const hal::HostReadFile> file;
    try {
        file = hal::OpenHostReadFile(source);
    } catch (const std::exception&) {
        throw LibraryError(LibraryErrorCode::not_found, "无法打开所选文件。", source);
    }
    std::uint64_t offset{};
    WriteImportFile(destination, file->Info().size, [&](std::span<std::byte> buffer) {
        const auto count = file->ReadAt(offset, buffer);
        offset += count;
        return count;
    }, nullptr);
}

std::unique_ptr<ImportSource> OpenZipArchive(const std::filesystem::path& path) {
    return std::make_unique<ZipSource>(path);
}

// Only the raw names are read: an APK with members a game archive would refuse is still an APK.
bool ZipHasRootManifest(const std::filesystem::path& path) {
    const auto file = hal::OpenHostReadFile(path);
    const auto members = ReadCentralDirectory(*file);
    return std::any_of(members.begin(), members.end(),
                       [](const RawMember& member) { return member.name == "AndroidManifest.xml"; });
}

ImportSourceKind DetectImportSource(const std::filesystem::path& path) {
    const auto head = Head(path);
    if (IsRar(head)) return ImportSourceKind::archive;
    if (!IsZip(head)) return ImportSourceKind::unknown;
    return ZipHasRootManifest(path) ? ImportSourceKind::apk : ImportSourceKind::archive;
}

std::unique_ptr<ImportSource> OpenArchive(const std::filesystem::path& path) {
    const auto head = Head(path);
    if (IsRar(head)) return OpenRarArchive(path);
    if (IsZip(head)) return OpenZipArchive(path);
    throw LibraryError(LibraryErrorCode::invalid_argument, "不是 ZIP 或 RAR 压缩包。", path);
}

}  // namespace ogplay::app
