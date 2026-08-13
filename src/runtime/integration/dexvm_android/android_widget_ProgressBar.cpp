#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ProgressBar(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/ProgressBar;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", handlers.handler_android_view_init);
    builder.Virtual("<init>", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V", handlers.handler_android_view_init);
    builder.Virtual("setMax", "(I)V", handlers.handler_android_widget_noop);
    builder.Virtual("setProgress", "(I)V", handlers.handler_android_widget_noop);
    builder.Virtual("setPadding", "(IIII)V", handlers.handler_android_widget_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
