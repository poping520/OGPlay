#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_MotionEvent(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/MotionEvent;");
}

}  // namespace ogplay::runtime::android_intrinsics
