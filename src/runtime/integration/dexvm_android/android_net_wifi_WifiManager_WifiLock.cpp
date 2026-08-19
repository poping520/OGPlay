#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiManager_WifiLock(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/wifi/WifiManager$WifiLock;", "Ljava/lang/Object;");
    builder.FinalMethod("acquire", "()V", GraphicsNoopHandler());
    builder.FinalMethod("release", "()V", GraphicsNoopHandler());
    builder.FinalMethod("isHeld", "()Z", TelephonyFalseHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
