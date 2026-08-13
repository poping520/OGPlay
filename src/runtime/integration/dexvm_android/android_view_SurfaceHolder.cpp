#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceHolder(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/SurfaceHolder;");
    builder.MarkInterface();
    builder.Virtual("addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", handlers.handler_android_surface_holder_add_callback);
    builder.Virtual("setType", "(I)V", handlers.handler_android_surface_holder_set_type);
    builder.Virtual("setFormat", "(I)V", handlers.handler_android_surface_holder_set_format);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
