#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_Drawable(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/drawable/Drawable;");
    builder.Super("Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
