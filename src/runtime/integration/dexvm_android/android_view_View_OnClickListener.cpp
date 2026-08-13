#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_View_OnClickListener(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/View$OnClickListener;");
    builder.MarkInterface();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
