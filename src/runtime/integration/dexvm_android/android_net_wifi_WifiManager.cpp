#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiManager(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/wifi/WifiManager;", "Ljava/lang/Object;");
    builder.FinalMethod("isWifiEnabled", "()Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.FinalMethod("getWifiState", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);  // WIFI_STATE_DISABLED
    });
    builder.FinalMethod("setWifiEnabled", "(Z)Z", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);  // There is no radio to enable.
    });
    builder.FinalMethod("getConnectionInfo", "()Landroid/net/wifi/WifiInfo;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("createWifiLock", "(ILjava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(call.vm.NewIntrinsicInstance(
                "Landroid/net/wifi/WifiManager$WifiLock;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
