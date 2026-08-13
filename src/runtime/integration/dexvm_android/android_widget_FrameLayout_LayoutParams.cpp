#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_FrameLayout_LayoutParams(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/FrameLayout$LayoutParams;");
}

}  // namespace ogplay::runtime::android_intrinsics
