#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_Toast(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/Toast;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("makeText", "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;", handlers.handler_android_toast_make_text);
    builder.Virtual("show", "()V", handlers.handler_android_toast_show);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
