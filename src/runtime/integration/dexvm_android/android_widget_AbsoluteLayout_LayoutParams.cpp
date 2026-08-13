#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_AbsoluteLayout_LayoutParams(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/AbsoluteLayout$LayoutParams;");
}

}  // namespace ogplay::runtime::android_intrinsics
