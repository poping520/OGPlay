#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_RelativeLayout_LayoutParams(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/RelativeLayout$LayoutParams;");
}

}  // namespace ogplay::runtime::android_intrinsics
