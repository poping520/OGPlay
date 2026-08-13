#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_Window(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/Window;");
}

}  // namespace ogplay::runtime::android_intrinsics
