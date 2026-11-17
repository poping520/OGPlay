#include "archive_source.h"

#include "ogplay/app/archive_import.h"
#include "ogplay/hal/fs.h"

#include <array>
#include <cstring>
#include <optional>
#include <vector>

namespace ogplay::app {
namespace {

constexpr std::uint32_t kCentralSignature = 0x02014b50, kEndSignature = 0x06054b50, kLocatorSignature = 0x07064b50,
                        kEnd64Signature = 0x06064b50;
constexpr std::size_t kEndSize = 22, kMaxComment = 0xFFFF;
constexpr std::uint64_t kMaxCentralDirectory = 256ULL << 20U;

[[nodiscard]] std::uint16_t U16(std::span<const std::byte> bytes, std::size_t at) {
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[at]) | std::to_integer<unsigned>(bytes[at + 1])
                                                                                 << 8U);
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

bool HasRootManifest(const hal::HostReadFile& file) {
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
        if (U32(locator, 4) != 0 || U32(locator, 16) > 1 || U32(record, 16) != 0 || U32(record, 20) != 0)
            throw LibraryError(LibraryErrorCode::invalid_argument, "暂不支持分卷压缩包，请先合并或解压。");
        count = U64(record, 32);
        directory_size = U64(record, 40);
        directory_offset = U64(record, 48);
    }
    if (directory_size > kMaxCentralDirectory) Broken("目录过大");
    const auto directory = ReadExact(file, directory_offset, static_cast<std::size_t>(directory_size));
    bool found = false;
    std::size_t at = 0;
    for (std::uint64_t index = 0; index < count; ++index) {
        if (at + 46 > directory.size() || U32(directory, at) != kCentralSignature) Broken("目录项无效");
        const std::size_t name_size = U16(directory, at + 28), extra_size = U16(directory, at + 30),
                          comment_size = U16(directory, at + 32);
        if (at + 46 + name_size + extra_size + comment_size > directory.size()) Broken("目录项越界");
        const std::string_view name(reinterpret_cast<const char*>(directory.data() + at + 46), name_size);
        found |= name == "AndroidManifest.xml";
        at += 46 + name_size + extra_size + comment_size;
    }
    return found;
}

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
    return Starts(head, std::string_view("Rar!\x1a\x07\x00", 7)) ||
           Starts(head, std::string_view("Rar!\x1a\x07\x01\x00", 8));
}

[[nodiscard]] bool Is7z(const std::array<std::byte, 8>& head) {
    return Starts(head, "7z\xbc\xaf\x27\x1c");
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
    WriteImportFile(
        destination, file->Info().size,
        [&](std::span<std::byte> buffer) {
            const auto count = file->ReadAt(offset, buffer);
            offset += count;
            return count;
        },
        nullptr);
}

// Only the raw names are read: an APK with members a game archive would refuse is still an APK.
bool ZipHasRootManifest(const std::filesystem::path& path) {
    return HasRootManifest(*hal::OpenHostReadFile(path));
}

ImportSourceKind DetectImportSource(const std::filesystem::path& path) {
    const auto head = Head(path);
    if (IsRar(head) || Is7z(head) || Starts(head, "PK\x07\x08") || Starts(head, "PK00"))
        return ImportSourceKind::archive;
    if (!IsZip(head)) return ImportSourceKind::unknown;
    return ZipHasRootManifest(path) ? ImportSourceKind::apk : ImportSourceKind::archive;
}

}  // namespace ogplay::app
