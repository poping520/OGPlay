#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_WindowManagerImpl(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/WindowManagerImpl;");
    builder.Super("Ljava/lang/Object;");
    builder.Implements("Landroid/view/WindowManager;");
    builder.Virtual("getDefaultDisplay", "()Landroid/view/Display;", handlers.handler_android_windowmanager_get_default_display);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
