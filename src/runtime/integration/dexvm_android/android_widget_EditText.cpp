#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_EditText(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/EditText;");
}

}  // namespace ogplay::runtime::android_intrinsics
