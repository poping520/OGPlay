#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_PaintDrawable(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/drawable/PaintDrawable;");
    builder.Super("Landroid/graphics/drawable/Drawable;");
    builder.Virtual("<init>", "(I)V", GraphicsNoopHandler());
    builder.Virtual("setCornerRadius", "(F)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
