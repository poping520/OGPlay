#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_Display(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/Display;");
}

}  // namespace ogplay::runtime::android_intrinsics
