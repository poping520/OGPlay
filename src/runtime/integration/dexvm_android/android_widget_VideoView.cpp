#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_VideoView(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/VideoView;");
}

}  // namespace ogplay::runtime::android_intrinsics
