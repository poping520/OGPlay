#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_WindowManagerImpl(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/WindowManagerImpl;");
}

}  // namespace ogplay::runtime::android_intrinsics
