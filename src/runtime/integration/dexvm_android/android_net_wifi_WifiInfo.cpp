#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiInfo(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/net/wifi/WifiInfo;");
    builder.Super("Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
