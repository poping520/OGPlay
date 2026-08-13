#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiInfo(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/net/wifi/WifiInfo;");
}

}  // namespace ogplay::runtime::android_intrinsics
