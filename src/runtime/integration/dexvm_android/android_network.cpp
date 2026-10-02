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
    return dx::IntrinsicEnumBuilder(
               "Landroid/net/NetworkInfo$State;", {"CONNECTED"})
        .Build();
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

namespace {
void AcquireWifiManagerLease(const Context& context, dx::IntrinsicContext& call,
                             const dx::VmObjectRef manager) {
    if (!manager.IsValid())
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "WifiManager is null"};
    if (context->wifi_lock_leases.contains(call.receiver.Value())) return;
    // WifiLock and MulticastLock share the manager's active lease quota.
    std::size_t active{};
    for (const auto& [owner, service] : context->wifi_lock_leases) {
        static_cast<void>(owner);
        if (service == manager) ++active;
    }
    if (active >= 50U)
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
            "Exceeded maximum number of wifi locks"};
    context->wifi_lock_leases[call.receiver.Value()] = manager;
}
void RequireMulticastPermission(const Context& context) {
    if (!context->granted_permissions.contains("android.permission.CHANGE_WIFI_MULTICAST_STATE"))
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
            "MulticastLock requires CHANGE_WIFI_MULTICAST_STATE"};
}
}  // namespace

Decl Declare_android_net_wifi_WifiManager_WifiLock(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/wifi/WifiManager$WifiLock;");
    builder.DirectMethod("nativeAcquire", "(Landroid/net/wifi/WifiManager;I)V",
        [context](dx::IntrinsicContext& call) {
            if (!context->granted_permissions.contains("android.permission.WAKE_LOCK"))
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "WifiLock requires WAKE_LOCK"};
            const auto mode = call.arguments[1].AsInt();
            if (mode < 1 || mode > 3)
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "unsupported WifiLock mode"};
            AcquireWifiManagerLease(context, call, call.arguments[0].ref);
            return dx::VmValue::Void();
        }, dx::kAccPrivate | dx::kAccNative);
    builder.DirectMethod("nativeRelease", "()V", [context](dx::IntrinsicContext& call) {
        context->wifi_lock_leases.erase(call.receiver.Value());
        return dx::VmValue::Void();
    }, dx::kAccPrivate | dx::kAccNative);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_net_wifi_WifiManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiManager_MulticastLock(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/wifi/WifiManager$MulticastLock;");
    builder.DirectMethod("nativeAcquire", "(Landroid/net/wifi/WifiManager;)V",
        [context](dx::IntrinsicContext& call) {
            RequireMulticastPermission(context);
            AcquireWifiManagerLease(context, call, call.arguments[0].ref);
            return dx::VmValue::Void();
        }, dx::kAccPrivate | dx::kAccNative);
    builder.DirectMethod("nativeRelease", "()V", [context](dx::IntrinsicContext& call) {
        RequireMulticastPermission(context);
        // Each lease has a guest owner; releasing it cannot release another lock.
        context->wifi_lock_leases.erase(call.receiver.Value());
        return dx::VmValue::Void();
    }, dx::kAccPrivate | dx::kAccNative);
    return std::move(builder).Build();
}

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
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
