#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_DialogInterface_OnDismissListener(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/DialogInterface$OnDismissListener;");
}

}  // namespace ogplay::runtime::android_intrinsics
