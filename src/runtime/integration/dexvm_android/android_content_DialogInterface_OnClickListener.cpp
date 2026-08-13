#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_DialogInterface_OnClickListener(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/DialogInterface$OnClickListener;");
}

}  // namespace ogplay::runtime::android_intrinsics
