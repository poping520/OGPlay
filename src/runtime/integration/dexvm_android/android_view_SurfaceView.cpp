#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceView(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/SurfaceView;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", handlers.handler_android_view_init);
    builder.Virtual("getHolder", "()Landroid/view/SurfaceHolder;", handlers.handler_android_surface_view_get_holder);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
