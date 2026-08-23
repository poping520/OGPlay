#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLSurfaceView_EGLConfigChooser(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Interface(
        "Landroid/opengl/GLSurfaceView$EGLConfigChooser;"))
        .Build();
}

}  // namespace ogplay::runtime::android_intrinsics
