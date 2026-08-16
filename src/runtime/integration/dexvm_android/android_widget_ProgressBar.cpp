#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ProgressBar(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/ProgressBar;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.Virtual("<init>", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V", ViewInitHandler(context));
    builder.Virtual("setMax", "(I)V", WidgetNoopHandler());
    builder.Virtual("setProgress", "(I)V", WidgetNoopHandler());
    builder.Virtual("setPadding", "(IIII)V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
