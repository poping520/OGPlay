#include "ogplay/hal/fs.h"

#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <system_error>

namespace ogplay::hal {
namespace {
struct Descriptor final {
    int fd;
    ~Descriptor() { if (fd >= 0) ::close(fd); }
};
[[nodiscard]] int OpenRetry(const char* path, int flags, mode_t mode = 0) {
    int fd;
    do { fd = ::open(path, flags, mode); } while (fd < 0 && errno == EINTR);
    return fd;
}
}  // namespace

bool TryCloneFile(const std::filesystem::path& source,
                  const std::filesystem::path& destination) {
    const Descriptor input{OpenRetry(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
    if (input.fd < 0) throw std::system_error(errno, std::generic_category(), "open clone source");
    Descriptor output{OpenRetry(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644)};
    if (output.fd < 0) throw std::system_error(errno, std::generic_category(), "create clone destination");
    if (::ioctl(output.fd, FICLONE, input.fd) == 0) return true;
    const int error = errno;
    ::close(output.fd);
    output.fd = -1;
    ::unlink(destination.c_str());
    // Reflink needs btrfs/XFS-style shared extents on one file system.
    if (error == EOPNOTSUPP || error == EXDEV || error == EINVAL || error == ENOTTY) return false;
    throw std::system_error(error, std::generic_category(), "FICLONE");
}

}  // namespace ogplay::hal
