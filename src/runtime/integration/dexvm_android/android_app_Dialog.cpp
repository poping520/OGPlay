#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_Dialog(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/app/Dialog;");
}

}  // namespace ogplay::runtime::android_intrinsics
