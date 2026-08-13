#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewTreeObserver_OnGlobalLayoutListener(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;");
    builder.MarkInterface();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
