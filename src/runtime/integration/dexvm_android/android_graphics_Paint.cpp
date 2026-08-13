#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Paint(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/Paint;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", handlers.handler_android_graphics_noop);
    builder.Virtual("<init>", "(I)V", handlers.handler_android_graphics_noop);
    builder.Virtual("setColor", "(I)V", handlers.handler_android_graphics_noop);
    builder.Virtual("setAntiAlias", "(Z)V", handlers.handler_android_graphics_noop);
    builder.Virtual("setTextSize", "(F)V", handlers.handler_android_graphics_noop);
    builder.Virtual("setTypeface", "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;", handlers.handler_android_paint_set_typeface);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
