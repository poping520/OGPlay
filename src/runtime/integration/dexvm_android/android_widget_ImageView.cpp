#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageView(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/ImageView;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", handlers.handler_android_view_init);
    builder.Virtual("setImageResource", "(I)V", handlers.handler_android_widget_noop);
    builder.Virtual("setScaleType", "(Landroid/widget/ImageView$ScaleType;)V", handlers.handler_android_widget_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
