#include "ogplay/hal/fs.h"

#include <cerrno>
#include <sys/clonefile.h>
#include <system_error>

namespace ogplay::hal {

bool TryCloneFile(const std::filesystem::path& source,
                  const std::filesystem::path& destination) {
    if (::clonefile(source.c_str(), destination.c_str(), CLONE_NOFOLLOW) == 0) return true;
    // Different volumes or non-APFS file systems cannot share extents.
    if (errno == ENOTSUP || errno == EXDEV) return false;
    throw std::system_error(errno, std::generic_category(), "clonefile");
}

}  // namespace ogplay::hal
