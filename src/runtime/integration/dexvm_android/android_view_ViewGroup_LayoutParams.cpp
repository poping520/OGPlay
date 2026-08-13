#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewGroup_LayoutParams(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/ViewGroup$LayoutParams;");
}

}  // namespace ogplay::runtime::android_intrinsics
