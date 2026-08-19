#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_ProgressDialog(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/ProgressDialog;", "Ljava/lang/Object;");
    builder.Constructor("(Landroid/content/Context;)V", WidgetNoopHandler());
    builder.FinalMethod("setMessage", "(Ljava/lang/CharSequence;)V", WidgetNoopHandler());
    builder.FinalMethod("setProgressStyle", "(I)V", WidgetNoopHandler());
    builder.FinalMethod("show", "()V", WidgetNoopHandler());
    builder.FinalMethod("dismiss", "()V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
