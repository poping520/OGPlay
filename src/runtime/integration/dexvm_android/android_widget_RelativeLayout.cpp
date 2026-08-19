#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_RelativeLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/RelativeLayout;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
