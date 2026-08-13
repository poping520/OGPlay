#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_Toast(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/Toast;");
}

}  // namespace ogplay::runtime::android_intrinsics
