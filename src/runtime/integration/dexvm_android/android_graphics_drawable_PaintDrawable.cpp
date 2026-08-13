#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_PaintDrawable(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/drawable/PaintDrawable;");
    builder.Super("Landroid/graphics/drawable/Drawable;");
    builder.Virtual("<init>", "(I)V", handlers.handler_android_graphics_noop);
    builder.Virtual("setCornerRadius", "(F)V", handlers.handler_android_graphics_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
