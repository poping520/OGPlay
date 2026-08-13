#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_ConnectivityManager(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/net/ConnectivityManager;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getActiveNetworkInfo", "()Landroid/net/NetworkInfo;", handlers.handler_android_connectivity_get_active_network_info);
    builder.Virtual("getNetworkInfo", "(I)Landroid/net/NetworkInfo;", handlers.handler_android_connectivity_get_network_info);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
