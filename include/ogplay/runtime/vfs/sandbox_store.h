#pragma once

// Per-title persistent sandbox storage (ADR-0020, docs/design/sandbox/).
// The only code that touches a sandbox directory: everything above it sees
// the VirtualFileSystem. Depends on the standard library alone.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::runtime {

struct SandboxConfig final {
    // Old-title saves live in the KB..MB range; the default is an order of
    // magnitude of headroom. Exceeding it is -ENOSPC, like a full sdcard.
    std::uint64_t byte_quota{256ULL * 1024ULL * 1024ULL};
    std::uint64_t maximum_files{65536};
};

// One overlay entry as it exists on the host, keyed by guest absolute path.
struct SandboxEntry final {
    std::string path;  // guest absolute path, already VFS-normalized
    std::uint64_t size{};
    bool is_directory{};
    // The guest deleted a base-layer file at this path; it must read as
    // absent even though the read-only layer still has it.
    bool is_tombstone{};
};

class SandboxStore final {
public:
    // Opens (creating if needed) <root>/<package>/ and loads the overlay
    // index. Leftover crash temporaries are removed; the count is reported
    // through TemporaryFilesRemoved so the caller can log it.
    [[nodiscard]] static std::unique_ptr<SandboxStore> Open(
        const std::filesystem::path& root, std::string_view package,
        SandboxConfig config = {});

    ~SandboxStore();
    SandboxStore(const SandboxStore&) = delete;
    SandboxStore& operator=(const SandboxStore&) = delete;

    // Index snapshot in guest-path order, so directory enumeration built on
    // top of it is deterministic.
    [[nodiscard]] std::vector<SandboxEntry> Entries() const;
    [[nodiscard]] std::vector<std::byte> ReadFile(
        std::string_view guest_path) const;

    // tmp + rename in the same directory: a crash leaves either the old or
    // the new contents, never a half-written file.
    void WriteFileAtomic(std::string_view guest_path,
                         std::span<const std::byte> contents);
    void WriteTombstone(std::string_view guest_path);
    void CreateDirectory(std::string_view guest_path);
    // Removes a file, tombstone or empty directory; absent is -ENOENT.
    // A guest rename is composed by the VFS overlay (write the new name,
    // tombstone the old one); the store has no rename of its own.
    void Remove(std::string_view guest_path);

    [[nodiscard]] std::uint64_t UsedBytes() const;
    [[nodiscard]] std::uint64_t FileCount() const;
    [[nodiscard]] std::uint64_t QuotaBytes() const;
    [[nodiscard]] const std::string& Package() const;
    [[nodiscard]] const std::filesystem::path& Directory() const;
    [[nodiscard]] std::uint64_t TemporaryFilesRemoved() const;
    // Diagnostic fact recorded in meta.toml; never used to pick behaviour.
    void RecordVersionCode(std::uint32_t version_code);

    // Host filename translation, exposed for its own tests: guest path
    // segments become host-safe names losslessly in both directions.
    [[nodiscard]] static std::string EscapeSegment(std::string_view segment);
    [[nodiscard]] static std::string UnescapeSegment(std::string_view segment);

private:
    class Impl;
    explicit SandboxStore(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
