#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_Button(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/Button;");
}

}  // namespace ogplay::runtime::android_intrinsics
