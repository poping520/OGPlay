#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebChromeClient(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/webkit/WebChromeClient;");
}

}  // namespace ogplay::runtime::android_intrinsics
