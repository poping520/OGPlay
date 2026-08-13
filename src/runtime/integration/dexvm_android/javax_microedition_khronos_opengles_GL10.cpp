#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_microedition_khronos_opengles_GL10(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljavax/microedition/khronos/opengles/GL10;");
    builder.MarkInterface();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
