#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_LinearLayout_LayoutParams(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/LinearLayout$LayoutParams;");
    builder.Super("Landroid/view/ViewGroup$LayoutParams;");
    builder.Virtual("<init>", "(II)V", handlers.handler_android_widget_noop);
    builder.Virtual("<init>", "(IIF)V", handlers.handler_android_widget_noop);
    builder.Virtual("setMargins", "(IIII)V", handlers.handler_android_widget_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
