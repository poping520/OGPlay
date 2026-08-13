#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiManager_WifiLock(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/net/wifi/WifiManager$WifiLock;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("acquire", "()V", handlers.handler_android_graphics_noop);
    builder.Virtual("release", "()V", handlers.handler_android_graphics_noop);
    builder.Virtual("isHeld", "()Z", handlers.handler_android_telephony_false);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
