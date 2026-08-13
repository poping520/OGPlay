#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_ProgressDialog(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/app/ProgressDialog;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", handlers.handler_android_widget_noop);
    builder.Virtual("setMessage", "(Ljava/lang/CharSequence;)V", handlers.handler_android_widget_noop);
    builder.Virtual("setProgressStyle", "(I)V", handlers.handler_android_widget_noop);
    builder.Virtual("show", "()V", handlers.handler_android_widget_noop);
    builder.Virtual("dismiss", "()V", handlers.handler_android_widget_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
