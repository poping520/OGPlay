#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_LinearLayout_LayoutParams(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/LinearLayout$LayoutParams;");
    builder.Super("Landroid/view/ViewGroup$LayoutParams;");
    builder.Virtual("<init>", "(II)V", WidgetNoopHandler());
    builder.Virtual("<init>", "(IIF)V", WidgetNoopHandler());
    builder.Virtual("setMargins", "(IIII)V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
