#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_LinearLayout_LayoutParams(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/LinearLayout$LayoutParams;");
}

}  // namespace ogplay::runtime::android_intrinsics
