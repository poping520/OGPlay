#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_View(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/View;");
}

}  // namespace ogplay::runtime::android_intrinsics
