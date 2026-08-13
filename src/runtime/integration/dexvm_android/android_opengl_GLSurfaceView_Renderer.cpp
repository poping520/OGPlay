#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLSurfaceView_Renderer(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/opengl/GLSurfaceView$Renderer;");
    builder.MarkInterface();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
