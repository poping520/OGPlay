#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLSurfaceView(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/opengl/GLSurfaceView;");
}

}  // namespace ogplay::runtime::android_intrinsics
