#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_KeyEvent(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/KeyEvent;");
}

}  // namespace ogplay::runtime::android_intrinsics
