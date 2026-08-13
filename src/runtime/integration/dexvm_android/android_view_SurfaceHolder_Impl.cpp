#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceHolder_Impl(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/SurfaceHolder$Impl;");
}

}  // namespace ogplay::runtime::android_intrinsics
