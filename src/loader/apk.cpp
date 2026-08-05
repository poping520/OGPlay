#include "ogplay/loader/apk.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace ogplay::loader {
namespace {

constexpr std::uint32_t kLocalSignature = 0x04034b50;
constexpr std::uint32_t kCentralSignature = 0x02014b50;
constexpr std::uint32_t kEndSignature = 0x06054b50;
constexpr std::size_t kEndSize = 22;
constexpr std::size_t kMaxCommentSize = 65535;

void RequireRange(const std::span<const std::byte> bytes, const std::size_t offset,
                  const std::size_t size, const std::string_view what) {
    if (offset > bytes.size() || size > bytes.size() - offset) {
        throw std::runtime_error(std::string(what) + " is outside APK bytes");
    }
}

std::uint16_t Read16(const std::span<const std::byte> bytes, const std::size_t offset) {
    RequireRange(bytes, offset, 2, "ZIP field");
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8U);
}

std::uint32_t Read32(const std::span<const std::byte> bytes, const std::size_t offset) {
    RequireRange(bytes, offset, 4, "ZIP field");
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << static_cast<unsigned>(index * 8);
    }
    return value;
}

std::string ReadName(const std::span<const std::byte> bytes, const std::size_t offset,
                     const std::size_t size) {
    RequireRange(bytes, offset, size, "ZIP entry name");
    std::string name;
    name.reserve(size);
    for (std::size_t index = 0; index < size; ++index) {
        const auto value = std::to_integer<std::uint8_t>(bytes[offset + index]);
        if (value == 0 || value >= 0x80) {
            throw std::runtime_error("APK entry name must be non-empty ASCII without NUL");
        }
        name.push_back(static_cast<char>(value));
    }
    if (name.empty() || name.front() == '/' || name.front() == '\\') {
        throw std::runtime_error("APK entry name is empty or absolute");
    }
    std::size_t begin{};
    while (begin < name.size()) {
        const auto end = name.find_first_of("/\\", begin);
        const auto segment = name.substr(begin, end == std::string::npos ? end : end - begin);
        if (segment == ".." || segment == ".") {
            throw std::runtime_error("APK entry name contains an unsafe path segment");
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return name;
}

std::uint32_t Crc32(const std::span<const std::byte> bytes) noexcept {
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const auto mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

std::size_t FindEnd(const std::span<const std::byte> bytes) {
    if (bytes.size() < kEndSize) throw std::runtime_error("APK has no ZIP end record");
    const auto first = bytes.size() > kEndSize + kMaxCommentSize
                           ? bytes.size() - kEndSize - kMaxCommentSize
                           : 0;
    for (auto offset = bytes.size() - kEndSize;; --offset) {
        if (Read32(bytes, offset) == kEndSignature &&
            Read16(bytes, offset + 20) == bytes.size() - offset - kEndSize) {
            return offset;
        }
        if (offset == first) break;
    }
    throw std::runtime_error("APK has no valid ZIP end record");
}

}  // namespace

ApkArchive ParseApkArchive(const std::span<const std::byte> bytes) {
    const auto end = FindEnd(bytes);
    if (Read16(bytes, end + 4) != 0 || Read16(bytes, end + 6) != 0) {
        throw std::runtime_error("multi-disk APK archives are unsupported");
    }
    const auto entry_count = Read16(bytes, end + 10);
    if (Read16(bytes, end + 8) != entry_count) {
        throw std::runtime_error("APK central directory entry counts disagree");
    }
    const auto central_size = Read32(bytes, end + 12);
    const auto central_offset = Read32(bytes, end + 16);
    RequireRange(bytes, central_offset, central_size, "APK central directory");
    if (static_cast<std::uint64_t>(central_offset) + central_size != end) {
        throw std::runtime_error("APK central directory does not end at the ZIP end record");
    }

    ApkArchive archive;
    archive.entries.reserve(entry_count);
    std::unordered_set<std::string> names;
    std::size_t cursor = central_offset;
    for (std::size_t index = 0; index < entry_count; ++index) {
        RequireRange(bytes, cursor, 46, "APK central directory entry");
        if (Read32(bytes, cursor) != kCentralSignature) {
            throw std::runtime_error("APK central directory signature is invalid");
        }
        const auto flags = Read16(bytes, cursor + 8);
        if ((flags & 1U) != 0) throw std::runtime_error("encrypted APK entries are unsupported");
        const auto name_size = Read16(bytes, cursor + 28);
        const auto extra_size = Read16(bytes, cursor + 30);
        const auto comment_size = Read16(bytes, cursor + 32);
        const auto record_size = static_cast<std::size_t>(46) + name_size + extra_size + comment_size;
        RequireRange(bytes, cursor, record_size, "APK central directory entry");
        auto name = ReadName(bytes, cursor + 46, name_size);
        if (!names.insert(name).second) throw std::runtime_error("APK contains duplicate entry names");
        archive.entries.push_back({
            .name = std::move(name),
            .compression_method = Read16(bytes, cursor + 10),
            .crc32 = Read32(bytes, cursor + 16),
            .compressed_size = Read32(bytes, cursor + 20),
            .uncompressed_size = Read32(bytes, cursor + 24),
            .local_header_offset = Read32(bytes, cursor + 42),
        });
        cursor += record_size;
    }
    if (cursor != static_cast<std::size_t>(central_offset) + central_size) {
        throw std::runtime_error("APK central directory size disagrees with entries");
    }
    return archive;
}

std::vector<std::byte> ReadStoredApkEntry(const std::span<const std::byte> bytes,
                                         const ApkArchive& archive,
                                         const std::string_view name) {
    const auto found = std::find_if(archive.entries.begin(), archive.entries.end(),
                                    [name](const ApkEntry& entry) { return entry.name == name; });
    if (found == archive.entries.end()) throw std::runtime_error("APK entry was not found");
    if (found->compression_method != 0 || found->compressed_size != found->uncompressed_size) {
        throw std::runtime_error("APK entry is not stored without compression");
    }
    const auto offset = static_cast<std::size_t>(found->local_header_offset);
    RequireRange(bytes, offset, 30, "APK local file header");
    if (Read32(bytes, offset) != kLocalSignature) {
        throw std::runtime_error("APK local file header signature is invalid");
    }
    if (Read16(bytes, offset + 8) != found->compression_method ||
        Read32(bytes, offset + 14) != found->crc32 ||
        Read32(bytes, offset + 18) != found->compressed_size ||
        Read32(bytes, offset + 22) != found->uncompressed_size) {
        throw std::runtime_error("APK local and central entry metadata disagree");
    }
    const auto local_name_size = Read16(bytes, offset + 26);
    const auto local_extra_size = Read16(bytes, offset + 28);
    if (ReadName(bytes, offset + 30, local_name_size) != found->name) {
        throw std::runtime_error("APK local and central entry names disagree");
    }
    const auto data_offset = offset + 30 + local_name_size + local_extra_size;
    RequireRange(bytes, data_offset, found->uncompressed_size, "APK stored entry data");
    const auto data = bytes.subspan(data_offset, found->uncompressed_size);
    if (Crc32(data) != found->crc32) throw std::runtime_error("APK stored entry CRC32 mismatch");
    return {data.begin(), data.end()};
}

}  // namespace ogplay::loader
