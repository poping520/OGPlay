// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_net_ConnectivityManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {
const AndroidOfflineConnectivity& RequireOfflineConnectivity(
    dx::IntrinsicContext& call, const Context& context, bool network_permission) {
    if (network_permission &&
        !context->granted_permissions.contains("android.permission.ACCESS_NETWORK_STATE"))
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "ACCESS_NETWORK_STATE is required"};
    if (!context->offline_connectivity || context->network_policy.enabled ||
        call.vm.Network().Policy().enabled) {
        if (auto* ledger = call.vm.Ledger())
            ledger->RecordUnimplemented("dexvm.connectivity_offline", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
            "offline connectivity facts are absent or an online policy requires a separate state provider"};
    }
    return *context->offline_connectivity;
}

// The SDK compile stubs hide these constructors/setters. The native fact
// factory invokes the original BootDex methods; it does not reimplement them.
dx::VmObjectRef NewOfflineWifiInfo(dx::IntrinsicContext& call) {
    auto& vm = call.vm;
    auto& linker = vm.Linker();
    const auto propagate = [&](const dx::VmCallOutcome& outcome) {
        if (outcome.exception.IsValid())
            throw dx::VmJavaThrow{linker.Class(outcome.exception_class).descriptor,
                                 outcome.exception_message, outcome.exception};
    };
    const auto type = linker.ResolveDescriptor("Landroid/net/NetworkInfo;");
    propagate(vm.EnsureClassInitialized(type));
    const auto info = vm.NewIntrinsicInstance("Landroid/net/NetworkInfo;");
    const auto root = vm.ProtectReferences(std::array{info});
    const auto type_name = vm.NewStringUtf8("WIFI");
    const auto type_name_root = vm.ProtectReferences(std::array{type_name});
    const auto subtype_name = vm.NewStringUtf8("");
    const auto strings_root = vm.ProtectReferences(std::array{subtype_name});
    const auto constructor = linker.FindDirectMethod(type, "<init>", "(IILjava/lang/String;Ljava/lang/String;)V");
    if (!constructor)
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "NetworkInfo constructor is unavailable");
    propagate(vm.Call(*constructor, std::array{dx::VmValue::Ref(info), dx::VmValue::Int(1),
        dx::VmValue::Int(0), dx::VmValue::Ref(type_name), dx::VmValue::Ref(subtype_name)}));
    const auto detailed_type = linker.ResolveDescriptor("Landroid/net/NetworkInfo$DetailedState;");
    propagate(vm.EnsureClassInitialized(detailed_type));
    const auto field = linker.FindFieldRecursive(detailed_type, "DISCONNECTED", "Landroid/net/NetworkInfo$DetailedState;");
    if (!field)
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "NetworkInfo state is unavailable");
    const auto disconnected = dx::VmObjectRef{linker.Class(detailed_type).static_storage[linker.Field(*field).slot]};
    static_cast<void>(CallAndroidMethod(vm, info, "setDetailedState",
        "(Landroid/net/NetworkInfo$DetailedState;Ljava/lang/String;Ljava/lang/String;)V",
        {dx::VmValue::Ref(disconnected), dx::VmValue::Ref(dx::VmObjectRef{}), dx::VmValue::Ref(dx::VmObjectRef{})}));
    static_cast<void>(CallAndroidMethod(vm, info, "setIsAvailable", "(Z)V", {dx::VmValue::Int(0)}));
    return info;
}
} // namespace

Decl Declare_android_net_ConnectivityManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/ConnectivityManager;");
    builder.DirectMethod("nativeRequireOffline", "()V", [context](dx::IntrinsicContext& call) {
        static_cast<void>(RequireOfflineConnectivity(call, context, true));
        return dx::VmValue::Void();
    }, dx::kAccPrivate | dx::kAccNative);
    builder.DirectMethod("nativeGetNetworkInfo", "(I)Landroid/net/NetworkInfo;", [context](dx::IntrinsicContext& call) {
        const auto& facts = RequireOfflineConnectivity(call, context, true);
        if (call.arguments[0].AsInt() != 1 || !facts.wifi_type_supported)
            return dx::VmValue::Ref(dx::VmObjectRef{});
        return dx::VmValue::Ref(NewOfflineWifiInfo(call));
    }, dx::kAccPrivate | dx::kAccNative);
    return std::move(builder).Build();
}

} // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_net_wifi_WifiInfo.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiInfo(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/wifi/WifiInfo;", "Ljava/lang/Object;",
                                                    {"Landroid/os/Parcelable;"});
    const auto wu_mMacAddress = builder.BoundInstanceField("mMacAddress", "Ljava/lang/String;", dx::kAccPrivate);
    builder.VirtualMethod("getMacAddress", "()Ljava/lang/String;", [wu_mMacAddress](dx::IntrinsicContext& c) {
        return dx::VmValue::Ref(dx::IntrinsicCall(c).GetRef(wu_mMacAddress));
    });
    const auto wu_mSupplicantState =
        builder.BoundInstanceField("mSupplicantState", "Landroid/net/wifi/SupplicantState;", dx::kAccPrivate);
    const auto wu_mBSSID = builder.BoundInstanceField("mBSSID", "Ljava/lang/String;", dx::kAccPrivate);
    const auto wu_mWifiSsid = builder.BoundInstanceField("mWifiSsid", "Landroid/net/wifi/WifiSsid;", dx::kAccPrivate);
    const auto wu_mNetworkId = builder.BoundInstanceField("mNetworkId", "I", dx::kAccPrivate);
    const auto wu_mHiddenSSID = builder.BoundInstanceField("mHiddenSSID", "Z", dx::kAccPrivate);
    const auto wu_mRssi = builder.BoundInstanceField("mRssi", "I", dx::kAccPrivate);
    const auto wu_mLinkSpeed = builder.BoundInstanceField("mLinkSpeed", "I", dx::kAccPrivate);
    const auto wu_mIpAddress = builder.BoundInstanceField("mIpAddress", "Ljava/net/InetAddress;", dx::kAccPrivate);
    const auto wu_mMeteredHint = builder.BoundInstanceField("mMeteredHint", "Z", dx::kAccPrivate);
    // AOSP 4.4 ROM line 96.
    builder.Constructor(
        "(Landroid/net/wifi/WifiInfo;)V",
        [wu_mSupplicantState, wu_mBSSID, wu_mWifiSsid, wu_mNetworkId, wu_mHiddenSSID, wu_mRssi, wu_mLinkSpeed,
         wu_mIpAddress, wu_mMacAddress, wu_mMeteredHint](dx::IntrinsicContext& c) {
            dx::IntrinsicCall api(c);
            const auto source = c.arguments[0].ref;
            if (!source.IsValid()) return dx::VmValue::Void();
            api.SetRef(wu_mSupplicantState, api.GetRef(wu_mSupplicantState, source));
            api.SetRef(wu_mBSSID, api.GetRef(wu_mBSSID, source));
            api.SetRef(wu_mWifiSsid, api.GetRef(wu_mWifiSsid, source));
            api.SetInt(wu_mNetworkId, api.GetInt(wu_mNetworkId, source));
            api.SetInt(wu_mHiddenSSID, api.GetInt(wu_mHiddenSSID, source));
            api.SetInt(wu_mRssi, api.GetInt(wu_mRssi, source));
            api.SetInt(wu_mLinkSpeed, api.GetInt(wu_mLinkSpeed, source));
            api.SetRef(wu_mIpAddress, api.GetRef(wu_mIpAddress, source));
            api.SetRef(wu_mMacAddress, api.GetRef(wu_mMacAddress, source));
            api.SetInt(wu_mMeteredHint, api.GetInt(wu_mMeteredHint, source));
            return dx::VmValue::Void();
        },
        dx::kAccPublic);

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
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/wifi/WifiManager;", "Ljava/lang/Object;");
    builder.FinalMethod("isWifiEnabled", "()Z",
        [context](dx::IntrinsicContext& call) {
            static_cast<void>(RequireOfflineConnectivity(call, context, false));
            return dx::VmValue::Int(0);
        });
    builder.FinalMethod("getWifiState", "()I", [context](dx::IntrinsicContext& call) {
        static_cast<void>(RequireOfflineConnectivity(call, context, false));
        return dx::VmValue::Int(1);  // WIFI_STATE_DISABLED
    });
    builder.FinalMethod("setWifiEnabled", "(Z)Z", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);  // There is no radio to enable.
    });
    builder.FinalMethod("getConnectionInfo", "()Landroid/net/wifi/WifiInfo;",
        [context](dx::IntrinsicContext& call) {
            static_cast<void>(RequireOfflineConnectivity(call, context, false));
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
