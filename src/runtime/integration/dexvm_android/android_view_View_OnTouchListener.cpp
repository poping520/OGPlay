#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_View_OnTouchListener(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/View$OnTouchListener;");
    builder.MarkInterface();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
