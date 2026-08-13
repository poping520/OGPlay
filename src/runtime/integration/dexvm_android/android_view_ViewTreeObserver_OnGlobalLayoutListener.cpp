#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewTreeObserver_OnGlobalLayoutListener(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;");
}

}  // namespace ogplay::runtime::android_intrinsics
