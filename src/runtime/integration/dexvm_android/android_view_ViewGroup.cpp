#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewGroup(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/ViewGroup;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("addView", "(Landroid/view/View;)V", handlers.handler_android_widget_noop);
    builder.Virtual("addView", "(Landroid/view/View;ILandroid/view/ViewGroup$LayoutParams;)V", handlers.handler_android_widget_noop);
    builder.Virtual("addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V", handlers.handler_android_widget_noop);
    builder.Virtual("removeView", "(Landroid/view/View;)V", handlers.handler_android_widget_noop);
    builder.Virtual("removeViews", "(II)V", handlers.handler_android_widget_noop);
    builder.Virtual("updateViewLayout", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V", handlers.handler_android_widget_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
