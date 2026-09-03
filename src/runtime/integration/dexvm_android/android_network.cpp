// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_net_ConnectivityManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_ConnectivityManager(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/ConnectivityManager;", "Ljava/lang/Object;");
    builder.FinalMethod("getActiveNetworkInfo", "()Landroid/net/NetworkInfo;",
        [](dx::IntrinsicContext&) {
            // Truthful offline fact: no active network (documented null).
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("getNetworkInfo", "(I)Landroid/net/NetworkInfo;",
        [](dx::IntrinsicContext&) {
            // No network of any type is connected on this platform.
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_net_NetworkInfo_State.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_NetworkInfo_State(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/NetworkInfo$State;", "Ljava/lang/Object;");
    builder.StaticField("CONNECTED", "Landroid/net/NetworkInfo$State;");
    builder.ClassInitializer([](dx::IntrinsicContext& call) {
        call.vm.SetIntrinsicStaticRef(
            "Landroid/net/NetworkInfo$State;", "CONNECTED",
            "Landroid/net/NetworkInfo$State;",
            call.vm.NewIntrinsicInstance(
                "Landroid/net/NetworkInfo$State;"));
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_net_NetworkInfo.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_NetworkInfo(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/NetworkInfo;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_net_Uri.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_Uri(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/Uri;", "Ljava/lang/Object;");
    builder.StaticMethod("parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/net/Uri;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_net_wifi_WifiInfo.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiInfo(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/wifi/WifiInfo;", "Ljava/lang/Object;");
    builder.FinalMethod("getMacAddress", "()Ljava/lang/String;",
        [](dx::IntrinsicContext&) {
            // AOSP returns the connection record's stored address. OGPlay has
            // no Wi-Fi radio or connection record, so the honest value is
            // the field default: null. Never expose a host adapter address.
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_net_wifi_WifiManager_WifiLock.cpp ----
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


// ---- migrated from android_net_wifi_WifiManager.cpp ----
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
