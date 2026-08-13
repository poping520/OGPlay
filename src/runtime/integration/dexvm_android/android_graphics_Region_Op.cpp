#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Region_Op(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/graphics/Region$Op;");
}

}  // namespace ogplay::runtime::android_intrinsics
