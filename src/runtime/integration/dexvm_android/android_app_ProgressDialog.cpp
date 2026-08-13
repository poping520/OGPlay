#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_ProgressDialog(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/app/ProgressDialog;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", WidgetNoopHandler());
    builder.Virtual("setMessage", "(Ljava/lang/CharSequence;)V", WidgetNoopHandler());
    builder.Virtual("setProgressStyle", "(I)V", WidgetNoopHandler());
    builder.Virtual("show", "()V", WidgetNoopHandler());
    builder.Virtual("dismiss", "()V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
