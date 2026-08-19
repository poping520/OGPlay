#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_PaintDrawable(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/PaintDrawable;", "Landroid/graphics/drawable/Drawable;");
    builder.Constructor("(I)V", GraphicsNoopHandler());
    builder.FinalMethod("setCornerRadius", "(F)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
