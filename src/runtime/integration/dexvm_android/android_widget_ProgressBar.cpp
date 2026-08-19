#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ProgressBar(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/ProgressBar;", "Landroid/view/View;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.Constructor("(Landroid/content/Context;Landroid/util/AttributeSet;I)V", ViewInitHandler(context));
    builder.FinalMethod("setMax", "(I)V", WidgetNoopHandler());
    builder.FinalMethod("setProgress", "(I)V", WidgetNoopHandler());
    builder.FinalMethod("setPadding", "(IIII)V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
