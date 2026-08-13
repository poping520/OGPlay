#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_EditText(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/EditText;");
    builder.Super("Landroid/widget/TextView;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", handlers.handler_android_view_init);
    builder.Virtual("getText", "()Landroid/text/Editable;", handlers.handler_android_edittext_get_editable);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
