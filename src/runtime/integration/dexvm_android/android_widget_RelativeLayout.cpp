#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_RelativeLayout(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/RelativeLayout;");
}

}  // namespace ogplay::runtime::android_intrinsics
