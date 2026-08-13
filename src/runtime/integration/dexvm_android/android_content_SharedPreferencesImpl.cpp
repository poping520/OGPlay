#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferencesImpl(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/SharedPreferencesImpl;");
}

}  // namespace ogplay::runtime::android_intrinsics
