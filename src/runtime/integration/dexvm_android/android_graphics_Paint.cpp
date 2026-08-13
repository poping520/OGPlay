#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Paint(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/graphics/Paint;");
}

}  // namespace ogplay::runtime::android_intrinsics
