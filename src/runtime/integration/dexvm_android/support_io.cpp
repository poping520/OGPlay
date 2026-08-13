// Migrated: stream/reader/writer/zip handler bodies now live in their
// per-class declaration files (java_io_*.cpp, java_nio_charset_Charset.cpp,
// java_util_zip_*.cpp). This empty batch keeps the assembly linking until
// the AndroidHandlers scaffold is removed.

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

void PopulateStreams(AndroidHandlers& handlers, const Context& context) {
    static_cast<void>(handlers);
    static_cast<void>(context);
}

}  // namespace ogplay::runtime::android_intrinsics
