#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_FrameLayout_LayoutParams(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/FrameLayout$LayoutParams;");
    builder.Super("Landroid/view/ViewGroup$LayoutParams;");
    builder.Virtual("<init>", "(II)V", handlers.handler_android_widget_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
