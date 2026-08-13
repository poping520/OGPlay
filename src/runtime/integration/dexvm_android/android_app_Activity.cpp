#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_Activity(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/app/Activity;");
}

}  // namespace ogplay::runtime::android_intrinsics
