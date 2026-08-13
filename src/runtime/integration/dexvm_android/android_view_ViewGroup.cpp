#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewGroup(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/ViewGroup;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("addView", "(Landroid/view/View;)V", WidgetNoopHandler());
    builder.Virtual("addView", "(Landroid/view/View;ILandroid/view/ViewGroup$LayoutParams;)V", WidgetNoopHandler());
    builder.Virtual("addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V", WidgetNoopHandler());
    builder.Virtual("removeView", "(Landroid/view/View;)V", WidgetNoopHandler());
    builder.Virtual("removeViews", "(II)V", WidgetNoopHandler());
    builder.Virtual("updateViewLayout", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
