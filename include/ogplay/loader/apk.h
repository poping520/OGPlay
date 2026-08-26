#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::loader {

struct ApkEntry {
    std::string name;
    std::uint16_t general_purpose_flags{};
    std::uint16_t compression_method{};
    std::uint32_t crc32{};
    std::uint32_t compressed_size{};
    std::uint32_t uncompressed_size{};
    std::uint32_t local_header_offset{};
};

struct ApkArchive {
    std::vector<ApkEntry> entries;
};

[[nodiscard]] ApkArchive ParseApkArchive(std::span<const std::byte> bytes);
[[nodiscard]] std::vector<std::byte> ReadApkEntry(
    std::span<const std::byte> bytes, const ApkArchive& archive, std::string_view name);
[[nodiscard]] std::vector<std::byte> ReadStoredApkEntry(
    std::span<const std::byte> bytes, const ApkArchive& archive, std::string_view name);
[[nodiscard]] std::uint64_t StoredApkEntryDataOffset(
    std::span<const std::byte> bytes, const ApkArchive& archive,
    std::string_view name);

}  // namespace ogplay::loader
