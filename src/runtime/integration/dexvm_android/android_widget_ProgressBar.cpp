#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ProgressBar(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/ProgressBar;");
}

}  // namespace ogplay::runtime::android_intrinsics
