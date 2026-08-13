#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLSurfaceView_Renderer(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/opengl/GLSurfaceView$Renderer;");
}

}  // namespace ogplay::runtime::android_intrinsics
