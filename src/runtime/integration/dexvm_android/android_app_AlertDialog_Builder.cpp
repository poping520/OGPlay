#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_AlertDialog_Builder(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/AlertDialog$Builder;", "Ljava/lang/Object;");
    builder.Constructor("(Landroid/content/Context;)V", WidgetNoopHandler());
    const auto self = dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        return Self(call);
    });
    builder.FinalMethod("setTitle", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;", self);
    builder.FinalMethod("setItems", "([Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;", self);
    builder.FinalMethod("create", "()Landroid/app/AlertDialog;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/app/AlertDialog;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
