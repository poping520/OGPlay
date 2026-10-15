#pragma once
#include <filesystem>
#include <memory>
#include <optional>
namespace ogplay::hal {
enum class FileLockMode { exclusive, shared };
// Nonblocking OS lock; shared holders coexist and exclude exclusive holders.
// A busy lock returns nullopt; IO errors throw.
// Never unlink a lock file while users can acquire it. Handles are not inherited.
class FileLock final {
public:
    ~FileLock();
    FileLock(FileLock&&) noexcept;
    FileLock& operator=(FileLock&&) noexcept;
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    static std::optional<FileLock> TryAcquire(const std::filesystem::path& path,
                                              FileLockMode mode = FileLockMode::exclusive);
private:
    struct Impl;
    explicit FileLock(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}
