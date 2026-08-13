#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Matrix(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/graphics/Matrix;");
}

}  // namespace ogplay::runtime::android_intrinsics
