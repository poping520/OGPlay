#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_Drawable(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/graphics/drawable/Drawable;");
}

}  // namespace ogplay::runtime::android_intrinsics
