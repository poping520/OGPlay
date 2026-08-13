#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_DialogInterface_OnCancelListener(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/DialogInterface$OnCancelListener;");
}

}  // namespace ogplay::runtime::android_intrinsics
