#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_microedition_khronos_egl_EGLConfig(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljavax/microedition/khronos/egl/EGLConfig;");
    builder.Super("Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
