#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_NetworkInfo(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/net/NetworkInfo;");
}

}  // namespace ogplay::runtime::android_intrinsics
