#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Rect(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/graphics/Rect;");
}

}  // namespace ogplay::runtime::android_intrinsics
