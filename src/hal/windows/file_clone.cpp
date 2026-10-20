#include "ogplay/hal/fs.h"

namespace ogplay::hal {

// ReFS block cloning needs FSCTL_DUPLICATE_EXTENTS_TO_FILE per region; callers
// fall back to streaming copies until that is worth supporting.
bool TryCloneFile(const std::filesystem::path&, const std::filesystem::path&) {
    return false;
}

}  // namespace ogplay::hal
