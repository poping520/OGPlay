#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_ConnectivityManager(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/net/ConnectivityManager;");
}

}  // namespace ogplay::runtime::android_intrinsics
