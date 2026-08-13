#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_Display(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/Display;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getWidth", "()I", handlers.handler_android_display_get_width);
    builder.Virtual("getHeight", "()I", handlers.handler_android_display_get_height);
    builder.Virtual("getRotation", "()I", handlers.handler_android_display_get_rotation);
    builder.Virtual("getOrientation", "()I", handlers.handler_android_display_get_rotation);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
