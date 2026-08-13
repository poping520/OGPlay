#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceHolder_Callback(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/SurfaceHolder$Callback;");
}

}  // namespace ogplay::runtime::android_intrinsics
