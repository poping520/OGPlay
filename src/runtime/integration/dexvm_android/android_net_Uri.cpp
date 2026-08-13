#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_Uri(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/net/Uri;");
}

}  // namespace ogplay::runtime::android_intrinsics
