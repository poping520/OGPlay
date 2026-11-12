#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_Drawable_Shell(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/Drawable;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    return std::move(builder).Build();
}

Decl Declare_android_graphics_drawable_StateListDrawable_Shell(const Context&) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/StateListDrawable;",
                                              "Landroid/graphics/drawable/DrawableContainer;");
    b.MergesBootClass();
    return std::move(b).Build();
}
}  // namespace ogplay::runtime::android_intrinsics
