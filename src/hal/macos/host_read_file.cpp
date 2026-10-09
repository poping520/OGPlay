#include "ogplay/hal/fs.h"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace ogplay::hal {
namespace {
class PosixReadFile final : public HostReadFile {
public:
    explicit PosixReadFile(const std::filesystem::path& path) {
        descriptor_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (descriptor_ < 0) throw std::system_error(errno, std::generic_category(), "open host backing");
        struct stat status{};
        const auto stat_result = ::fstat(descriptor_, &status);
        if (stat_result != 0 || !S_ISREG(status.st_mode) || status.st_size < 0) {
            const int error = stat_result != 0 ? errno : EINVAL;
            ::close(descriptor_);
            descriptor_ = -1;
            throw std::system_error(error, std::generic_category(), "host backing must be regular");
        }
        info_ = {{static_cast<std::uint64_t>(status.st_dev), static_cast<std::uint64_t>(status.st_ino), 0},
                 static_cast<std::uint64_t>(status.st_size)};
    }
    ~PosixReadFile() override { ::close(descriptor_); }
    HostReadFileInfo Info() const noexcept override { return info_; }
    std::size_t ReadAt(std::uint64_t offset, std::span<std::byte> output,
                       std::stop_token stop) const override {
        if (offset > static_cast<std::uint64_t>((std::numeric_limits<off_t>::max)()) ||
            output.size() > static_cast<std::uint64_t>((std::numeric_limits<off_t>::max)()) - offset)
            throw std::system_error(EOVERFLOW, std::generic_category(), "host read offset");
        std::size_t total{};
        while (total < output.size()) {
            if (stop.stop_requested()) throw std::system_error(ECANCELED, std::generic_category(), "host read cancelled");
            const auto chunk = std::min<std::size_t>(output.size() - total, 65536U);
            const auto count = ::pread(descriptor_, output.data() + total, chunk, static_cast<off_t>(offset));
            if (count < 0) {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "host positioned read");
            }
            total += static_cast<std::size_t>(count);
            offset += static_cast<std::uint64_t>(count);
            if (static_cast<std::size_t>(count) < chunk) break;
        }
        return total;
    }
private:
    int descriptor_{-1};
    HostReadFileInfo info_;
};
} // namespace

std::shared_ptr<const HostReadFile> OpenHostReadFile(const std::filesystem::path& path) {
    return std::make_shared<PosixReadFile>(path);
}
} // namespace ogplay::hal
