#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageButton(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/ImageButton;");
}

}  // namespace ogplay::runtime::android_intrinsics
