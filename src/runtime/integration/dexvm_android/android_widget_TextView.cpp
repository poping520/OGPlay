#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_TextView(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/TextView;");
}

}  // namespace ogplay::runtime::android_intrinsics
