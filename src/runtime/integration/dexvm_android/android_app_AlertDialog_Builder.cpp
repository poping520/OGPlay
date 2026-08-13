#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_AlertDialog_Builder(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/app/AlertDialog$Builder;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", handlers.handler_android_widget_noop);
    builder.Virtual("setTitle", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;", handlers.handler_android_widget_self);
    builder.Virtual("setItems", "([Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;", handlers.handler_android_widget_self);
    builder.Virtual("create", "()Landroid/app/AlertDialog;", handlers.handler_android_dialog_create);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
