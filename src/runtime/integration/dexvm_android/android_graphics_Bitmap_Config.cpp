#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Bitmap_Config(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/graphics/Bitmap$Config;");
}

}  // namespace ogplay::runtime::android_intrinsics
