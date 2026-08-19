#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLSurfaceView_Renderer(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/opengl/GLSurfaceView$Renderer;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
