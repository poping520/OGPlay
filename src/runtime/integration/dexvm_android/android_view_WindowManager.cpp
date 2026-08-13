#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_WindowManager(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/WindowManager;");
}

}  // namespace ogplay::runtime::android_intrinsics
