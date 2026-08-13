#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_Window(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/Window;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("setFlags", "(II)V", handlers.handler_android_window_noop);
    builder.Virtual("addFlags", "(I)V", handlers.handler_android_window_noop_add);
    builder.Virtual("clearFlags", "(I)V", handlers.handler_android_window_noop_clear);
    builder.Virtual("getAttributes", "()Landroid/view/WindowManager$LayoutParams;", handlers.handler_android_window_get_attributes);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
