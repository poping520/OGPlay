#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Canvas(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/graphics/Canvas;");
}

}  // namespace ogplay::runtime::android_intrinsics
