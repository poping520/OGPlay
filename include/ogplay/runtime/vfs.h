#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::runtime {

struct VfsOpenOptions final {
    bool read{};
    bool write{};
    bool create{};
    bool truncate{};
};

enum class VfsSource : std::uint8_t { runtime, apk, obb, external };

struct VfsMountEntry final {
    std::string path;
    std::vector<std::byte> contents;
};

enum class VfsSeekWhence : std::uint8_t { begin, current, end };

struct VfsFileInfo final {
    std::uint64_t size{};
    bool writable{};
    VfsSource source{VfsSource::runtime};
};

class VfsError final : public std::runtime_error {
public:
    VfsError(std::int32_t error_number, std::string message);
    [[nodiscard]] std::int32_t ErrorNumber() const noexcept {
        return error_number_;
    }

private:
    std::int32_t error_number_{};
};

class VirtualFileSystem final {
public:
    VirtualFileSystem();
    ~VirtualFileSystem();
    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

    void PutFile(std::string_view path, std::span<const std::byte> contents,
                 bool writable);
    void Mount(VfsSource source, std::string_view root,
               std::span<const VfsMountEntry> entries);
    [[nodiscard]] VfsFileInfo Stat(std::string_view path) const;
    [[nodiscard]] std::int32_t Open(std::string_view path,
                                    VfsOpenOptions options);
    [[nodiscard]] std::size_t Read(std::int32_t descriptor,
                                   std::span<std::byte> destination);
    [[nodiscard]] std::size_t Write(std::int32_t descriptor,
                                    std::span<const std::byte> source);
    [[nodiscard]] std::uint64_t Seek(std::int32_t descriptor,
                                     std::int64_t offset,
                                     VfsSeekWhence whence);
    void Close(std::int32_t descriptor);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
