#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_TableLayout(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/TableLayout;");
    builder.Super("Landroid/view/ViewGroup;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", handlers.handler_android_view_init);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
