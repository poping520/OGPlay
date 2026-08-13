#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_ProgressDialog(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/app/ProgressDialog;");
}

}  // namespace ogplay::runtime::android_intrinsics
