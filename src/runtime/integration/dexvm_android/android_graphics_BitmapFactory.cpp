#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_BitmapFactory(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/graphics/BitmapFactory;");
}

}  // namespace ogplay::runtime::android_intrinsics
