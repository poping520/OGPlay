#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_FrameLayout(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/FrameLayout;");
}

}  // namespace ogplay::runtime::android_intrinsics
