#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_TableLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TableLayout;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
