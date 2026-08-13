#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_PaintDrawable(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/graphics/drawable/PaintDrawable;");
}

}  // namespace ogplay::runtime::android_intrinsics
