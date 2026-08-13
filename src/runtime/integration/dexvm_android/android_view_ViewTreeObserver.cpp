#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewTreeObserver(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/ViewTreeObserver;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("addOnGlobalLayoutListener", "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V", handlers.handler_android_view_tree_add_global_listener);
    builder.Virtual("removeGlobalOnLayoutListener", "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V", handlers.handler_android_view_tree_remove_global_listener);
    builder.Virtual("removeOnGlobalLayoutListener", "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V", handlers.handler_android_view_tree_remove_global_listener);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
