#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_AlertDialog_Builder(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/app/AlertDialog$Builder;");
}

}  // namespace ogplay::runtime::android_intrinsics
