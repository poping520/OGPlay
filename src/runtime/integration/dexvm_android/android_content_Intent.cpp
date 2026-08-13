#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_Intent(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/Intent;");
}

}  // namespace ogplay::runtime::android_intrinsics
