#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_AlertDialog(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/app/AlertDialog;");
}

}  // namespace ogplay::runtime::android_intrinsics
