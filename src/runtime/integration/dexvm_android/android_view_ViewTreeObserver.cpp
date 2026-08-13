#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewTreeObserver(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/ViewTreeObserver;");
}

}  // namespace ogplay::runtime::android_intrinsics
