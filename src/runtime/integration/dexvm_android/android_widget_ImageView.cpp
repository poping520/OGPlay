#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageView(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/ImageView;");
}

}  // namespace ogplay::runtime::android_intrinsics
