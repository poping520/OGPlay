#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiManager_WifiLock(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/net/wifi/WifiManager$WifiLock;");
}

}  // namespace ogplay::runtime::android_intrinsics
