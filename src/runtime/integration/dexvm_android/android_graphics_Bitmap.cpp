#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Bitmap(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/graphics/Bitmap;");
}

}  // namespace ogplay::runtime::android_intrinsics
