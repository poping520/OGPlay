#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewGroup_LayoutParams(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/ViewGroup$LayoutParams;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(II)V", handlers.handler_android_widget_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
