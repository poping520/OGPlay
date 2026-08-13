#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiManager_WifiLock(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/net/wifi/WifiManager$WifiLock;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("acquire", "()V", GraphicsNoopHandler());
    builder.Virtual("release", "()V", GraphicsNoopHandler());
    builder.Virtual("isHeld", "()Z", TelephonyFalseHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
