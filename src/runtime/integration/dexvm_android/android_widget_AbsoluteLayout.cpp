#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_AbsoluteLayout(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/AbsoluteLayout;");
    builder.Super("Landroid/view/ViewGroup;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", ViewInitHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
