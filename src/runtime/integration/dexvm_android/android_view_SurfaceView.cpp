#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceView(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/SurfaceView;");
}

}  // namespace ogplay::runtime::android_intrinsics
