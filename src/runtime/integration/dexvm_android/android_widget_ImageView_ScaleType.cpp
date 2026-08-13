#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageView_ScaleType(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/ImageView$ScaleType;");
}

}  // namespace ogplay::runtime::android_intrinsics
