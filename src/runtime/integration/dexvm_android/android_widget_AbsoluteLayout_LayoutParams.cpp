#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_AbsoluteLayout_LayoutParams(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/AbsoluteLayout$LayoutParams;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(IIII)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
