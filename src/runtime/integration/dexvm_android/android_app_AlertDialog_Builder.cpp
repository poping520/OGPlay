#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_AlertDialog_Builder(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/app/AlertDialog$Builder;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", WidgetNoopHandler());
    const auto self = dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        return Self(call);
    });
    builder.Virtual("setTitle", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;", self);
    builder.Virtual("setItems", "([Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;", self);
    builder.Virtual("create", "()Landroid/app/AlertDialog;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/app/AlertDialog;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
