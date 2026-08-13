#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_View_OnClickListener(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/View$OnClickListener;");
}

}  // namespace ogplay::runtime::android_intrinsics
