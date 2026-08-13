#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebView(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/webkit/WebView;");
}

}  // namespace ogplay::runtime::android_intrinsics
