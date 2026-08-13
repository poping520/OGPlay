#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_View_OnTouchListener(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/View$OnTouchListener;");
}

}  // namespace ogplay::runtime::android_intrinsics
