#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebViewClient(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/webkit/WebViewClient;");
}

}  // namespace ogplay::runtime::android_intrinsics
