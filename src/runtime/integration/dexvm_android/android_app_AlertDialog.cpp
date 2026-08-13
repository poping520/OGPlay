#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_AlertDialog(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/app/AlertDialog;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("show", "()V", handlers.handler_android_widget_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
