#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_AlertDialog(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/AlertDialog;", "Ljava/lang/Object;");
    builder.FinalMethod("show", "()V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
