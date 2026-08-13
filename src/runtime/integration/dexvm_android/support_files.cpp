// Migrated: the java.io.File / FileWriter / FileOutputStream /
// DataOutputStream / Environment / StatFs handler bodies now live in their
// per-class declaration files; the shared VFS helpers moved to shared.h.
// This empty batch keeps the assembly linking until the AndroidHandlers
// scaffold is removed.

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

void PopulateFiles(AndroidHandlers& handlers, const Context& context) {
    static_cast<void>(handlers);
    static_cast<void>(context);
}

}  // namespace ogplay::runtime::android_intrinsics
