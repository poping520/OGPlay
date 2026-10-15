#include "ogplay/hal/file_lock.h"
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <system_error>
namespace ogplay::hal {
struct FileLock::Impl {
    int fd;
    explicit Impl(int value) : fd(value) {}
    ~Impl() { ::close(fd); }
};
FileLock::FileLock(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FileLock::~FileLock() = default;
FileLock::FileLock(FileLock&&) noexcept = default;
FileLock& FileLock::operator=(FileLock&&) noexcept = default;
std::optional<FileLock> FileLock::TryAcquire(const std::filesystem::path& path, const FileLockMode mode) {
    int fd;
    do { fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600); } while (fd < 0 && errno == EINTR);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "open file lock");
    auto owned = std::make_unique<Impl>(fd);
    struct stat status{};
    if (::fstat(fd, &status) < 0) throw std::system_error(errno, std::generic_category(), "stat file lock");
    if (!S_ISREG(status.st_mode)) throw std::runtime_error("lock path must be a regular file");
    int result;
    do { result = ::flock(fd, (mode == FileLockMode::shared ? LOCK_SH : LOCK_EX) | LOCK_NB); } while (result < 0 && errno == EINTR);
    if (result < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) return std::nullopt;
        throw std::system_error(errno, std::generic_category(), "acquire file lock");
    }
    return FileLock(std::move(owned));
}
}
