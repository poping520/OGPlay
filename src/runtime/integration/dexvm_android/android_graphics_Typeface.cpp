#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Typeface(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/graphics/Typeface;");
}

}  // namespace ogplay::runtime::android_intrinsics
