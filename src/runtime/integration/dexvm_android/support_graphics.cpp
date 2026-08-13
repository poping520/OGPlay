// Migrated: Bitmap/Canvas and widget handler bodies now live in their
// per-class declaration files (android_graphics_Bitmap.cpp,
// android_widget_TextView.cpp, and friends). These empty batches keep the
// assembly linking until the AndroidHandlers scaffold is removed.

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

void PopulateGraphicsBitmaps(AndroidHandlers& handlers,
                             const Context& context) {
    static_cast<void>(handlers);
    static_cast<void>(context);
}

void PopulateWidgets(AndroidHandlers& handlers,
                     const Context& context) {
    static_cast<void>(handlers);
    static_cast<void>(context);
}

}  // namespace ogplay::runtime::android_intrinsics
