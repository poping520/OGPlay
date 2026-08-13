#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_microedition_khronos_opengles_GL10(const Context& context) {
    return DeclareAndroidClass(context, "Ljavax/microedition/khronos/opengles/GL10;");
}

}  // namespace ogplay::runtime::android_intrinsics
