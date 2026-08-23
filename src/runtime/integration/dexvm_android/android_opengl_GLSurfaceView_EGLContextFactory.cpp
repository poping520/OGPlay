#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLSurfaceView_EGLContextFactory(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Interface(
        "Landroid/opengl/GLSurfaceView$EGLContextFactory;"))
        .Build();
}

}  // namespace ogplay::runtime::android_intrinsics
