#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Rect(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/Rect;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("left", "I", false);
    builder.Field("top", "I", false);
    builder.Field("right", "I", false);
    builder.Field("bottom", "I", false);
    builder.Virtual("<init>", "()V", handlers.handler_android_graphics_noop);
    builder.Virtual("width", "()I", handlers.handler_android_rect_width);
    builder.Virtual("height", "()I", handlers.handler_android_rect_height);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
