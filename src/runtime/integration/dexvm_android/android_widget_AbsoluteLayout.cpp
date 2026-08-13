#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_AbsoluteLayout(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/AbsoluteLayout;");
}

}  // namespace ogplay::runtime::android_intrinsics
