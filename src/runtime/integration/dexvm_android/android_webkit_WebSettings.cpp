#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebSettings(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/webkit/WebSettings;");
}

}  // namespace ogplay::runtime::android_intrinsics
