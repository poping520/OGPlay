#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_LinearLayout(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/LinearLayout;");
}

}  // namespace ogplay::runtime::android_intrinsics
