#pragma once

#include "ogplay/hal/fs.h"
#include "ogplay/runtime/vfs/vfs.h"

#include <atomic>
#include <map>
#include <mutex>
#include <optional>

namespace ogplay::runtime {
class HostFileBacking;

class HostFilePool final {
public:
    explicit HostFilePool(std::uint32_t limit);
    [[nodiscard]] std::uint64_t AllocateKey();
    [[nodiscard]] std::shared_ptr<const hal::HostReadFile> Acquire(HostFileBacking& backing);
    void AddStatistics(VfsIoStatistics& statistics) const noexcept;
private:
    struct Entry final {
        std::shared_ptr<const hal::HostReadFile> file;
        std::uint64_t used{};
    };
    std::mutex mutex_;
    std::map<std::uint64_t, Entry> entries_;
    std::uint32_t limit_;
    std::uint64_t next_key_{}, clock_{};
    std::atomic_uint64_t count_{}, high_water_{}, evictions_{};
};

class HostFileBacking final {
public:
    HostFileBacking(std::shared_ptr<HostFilePool> pool,
                    std::filesystem::path root, std::filesystem::path path,
                    std::uint64_t size);
    [[nodiscard]] std::shared_ptr<const hal::HostReadFile> Pin();
    [[nodiscard]] std::size_t ReadAt(std::uint64_t offset,
        std::span<std::byte> destination, std::stop_token stop = {});
private:
    friend class HostFilePool;
    std::shared_ptr<HostFilePool> pool_;
    std::filesystem::path root_, path_;
    std::uint64_t size_, key_;
    // Protected by pool mutex. Bound on first successful guest open.
    std::optional<hal::HostReadFileInfo> identity_;
};
} // namespace ogplay::runtime
