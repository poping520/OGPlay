#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiInfo(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/net/wifi/WifiInfo;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getMacAddress", "()Ljava/lang/String;",
        [](dx::IntrinsicContext&) {
            // AOSP returns the connection record's stored address. OGPlay has
            // no Wi-Fi radio or connection record, so the honest value is
            // the field default: null. Never expose a host adapter address.
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
