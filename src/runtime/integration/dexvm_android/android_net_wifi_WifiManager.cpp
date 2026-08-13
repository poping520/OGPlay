#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiManager(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/net/wifi/WifiManager;");
}

}  // namespace ogplay::runtime::android_intrinsics
