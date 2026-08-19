#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_AbsoluteLayout_LayoutParams(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/AbsoluteLayout$LayoutParams;", "Ljava/lang/Object;");
    builder.Constructor("(IIII)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
