#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiManager(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/net/wifi/WifiManager;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("isWifiEnabled", "()Z", handlers.handler_android_wifi_is_enabled);
    builder.Virtual("getWifiState", "()I", handlers.handler_android_wifi_get_state);
    builder.Virtual("setWifiEnabled", "(Z)Z", handlers.handler_android_wifi_set_enabled);
    builder.Virtual("getConnectionInfo", "()Landroid/net/wifi/WifiInfo;", handlers.handler_android_wifi_get_connection_info);
    builder.Virtual("createWifiLock", "(ILjava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;", handlers.handler_android_wifi_create_lock);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
