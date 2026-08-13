#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferences(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/SharedPreferences;");
}

}  // namespace ogplay::runtime::android_intrinsics
