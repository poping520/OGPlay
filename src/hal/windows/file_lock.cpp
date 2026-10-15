#include "ogplay/hal/file_lock.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <system_error>
namespace ogplay::hal {
struct FileLock::Impl {
    HANDLE handle;
    explicit Impl(HANDLE value) : handle(value) {}
    ~Impl() { CloseHandle(handle); }
};
FileLock::FileLock(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FileLock::~FileLock() = default;
FileLock::FileLock(FileLock&&) noexcept = default;
FileLock& FileLock::operator=(FileLock&&) noexcept = default;
std::optional<FileLock> FileLock::TryAcquire(const std::filesystem::path& path, const FileLockMode mode) {
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "open file lock");
    auto owned = std::make_unique<Impl>(handle);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "stat file lock");
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0) throw std::runtime_error("lock path must be a regular file");
    OVERLAPPED overlap{};
    if (!LockFileEx(handle, (mode == FileLockMode::exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0) | LOCKFILE_FAIL_IMMEDIATELY,
                    0, 1, 0, &overlap)) {
        const auto error = GetLastError();
        if (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING) return std::nullopt;
        throw std::system_error(static_cast<int>(error), std::system_category(), "acquire file lock");
    }
    return FileLock(std::move(owned));
}
}
