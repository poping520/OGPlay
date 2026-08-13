#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_microedition_khronos_egl_EGLConfig(const Context& context) {
    return DeclareAndroidClass(context, "Ljavax/microedition/khronos/egl/EGLConfig;");
}

}  // namespace ogplay::runtime::android_intrinsics
