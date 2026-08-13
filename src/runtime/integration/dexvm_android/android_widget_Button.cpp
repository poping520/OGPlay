#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_Button(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/Button;");
    builder.Super("Landroid/widget/TextView;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", ViewInitHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
