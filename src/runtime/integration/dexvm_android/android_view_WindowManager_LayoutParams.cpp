#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_WindowManager_LayoutParams(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/WindowManager$LayoutParams;");
}

}  // namespace ogplay::runtime::android_intrinsics
