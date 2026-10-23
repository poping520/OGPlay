#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
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

struct ApkEntryRangeStatistics final {
    std::uint64_t validation_scan_bytes{};
    std::uint64_t copied_bytes{};
};

[[nodiscard]] ApkArchive ParseApkArchive(std::span<const std::byte> bytes);
[[nodiscard]] std::vector<std::byte> ReadApkEntry(
    std::span<const std::byte> bytes, const ApkArchive& archive, std::string_view name);
[[nodiscard]] std::vector<std::byte> ReadStoredApkEntry(
    std::span<const std::byte> bytes, const ApkArchive& archive, std::string_view name);
[[nodiscard]] std::uint64_t StoredApkEntryDataOffset(
    std::span<const std::byte> bytes, const ApkArchive& archive,
    std::string_view name);
// Checks central sizes and local payload bounds, then borrows the STORED payload.
// As in API19 ZIP readers, redundant local metadata and CRC are not compared. Caller
// must keep the APK bytes alive and immutable for the lifetime of this view.
[[nodiscard]] std::span<const std::byte> ValidatedStoredApkEntryData(
    std::span<const std::byte> bytes, const ApkArchive& archive,
    std::string_view name, std::stop_token stop = {});
// Checks ZIP bounds and Deflate output size, then copies the requested window.
// Stored entries borrow payload and do not scan it; validation_scan_bytes counts
// only Deflate output scanned. CRC is not a read-time integrity contract.
[[nodiscard]] std::size_t ReadApkEntryRange(
    std::span<const std::byte> bytes, const ApkArchive& archive,
    std::string_view name, std::uint64_t offset,
    std::span<std::byte> destination, std::stop_token stop = {},
    ApkEntryRangeStatistics* statistics = nullptr);

}  // namespace ogplay::loader
