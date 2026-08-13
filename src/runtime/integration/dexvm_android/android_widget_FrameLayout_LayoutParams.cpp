#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_FrameLayout_LayoutParams(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/FrameLayout$LayoutParams;");
    builder.Super("Landroid/view/ViewGroup$LayoutParams;");
    builder.Virtual("<init>", "(II)V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
