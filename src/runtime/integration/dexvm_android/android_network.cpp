// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_net_ConnectivityManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {
const AndroidOfflineConnectivity& RequireOfflineConnectivity(dx::IntrinsicContext& call, const Context& context,
                                                             bool network_permission) {
    if (network_permission && !context->granted_permissions.contains("android.permission.ACCESS_NETWORK_STATE"))
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "ACCESS_NETWORK_STATE is required"};
    if (!context->offline_connectivity || context->network_policy.enabled || call.vm.Network().Policy().enabled) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.connectivity_offline", 0);
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            "offline connectivity facts are absent or an online policy requires a separate state provider"};
    }
    return *context->offline_connectivity;
}

// The SDK compile stubs hide these constructors/setters. The native fact
// factory invokes the original BootDex methods; it does not reimplement them.
dx::VmObjectRef NewOfflineWifiNetworkInfo(dx::IntrinsicContext& call) {
    auto& vm = call.vm;
    auto& linker = vm.Linker();
    const auto propagate = [&](const dx::VmCallOutcome& outcome) {
        if (outcome.exception.IsValid())
            throw dx::VmJavaThrow{linker.Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
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
    propagate(vm.Call(*constructor, std::array{dx::VmValue::Ref(info), dx::VmValue::Int(1), dx::VmValue::Int(0),
                                               dx::VmValue::Ref(type_name), dx::VmValue::Ref(subtype_name)}));
    const auto detailed_type = linker.ResolveDescriptor("Landroid/net/NetworkInfo$DetailedState;");
    propagate(vm.EnsureClassInitialized(detailed_type));
    const auto field =
        linker.FindFieldRecursive(detailed_type, "DISCONNECTED", "Landroid/net/NetworkInfo$DetailedState;");
    if (!field) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "NetworkInfo state is unavailable");
    const auto disconnected = dx::VmObjectRef{linker.Class(detailed_type).static_storage[linker.Field(*field).slot]};
    static_cast<void>(CallAndroidMethod(
        vm, info, "setDetailedState", "(Landroid/net/NetworkInfo$DetailedState;Ljava/lang/String;Ljava/lang/String;)V",
        {dx::VmValue::Ref(disconnected), dx::VmValue::Ref(dx::VmObjectRef{}), dx::VmValue::Ref(dx::VmObjectRef{})}));
    static_cast<void>(CallAndroidMethod(vm, info, "setIsAvailable", "(Z)V", {dx::VmValue::Int(0)}));
    return info;
}
}  // namespace

Decl Declare_local_ConnectivityService(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalConnectivityService;",
                                                    "Landroid/net/IConnectivityManager$Stub;");
    builder.VirtualMethod("getProxy", "()Landroid/net/ProxyProperties;", [context](dx::IntrinsicContext& call) {
        static_cast<void>(RequireOfflineConnectivity(call, context, false));
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    builder.VirtualMethod("getActiveNetworkInfo", "()Landroid/net/NetworkInfo;", [context](dx::IntrinsicContext& call) {
        static_cast<void>(RequireOfflineConnectivity(call, context, true));
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    builder.VirtualMethod("getNetworkInfo", "(I)Landroid/net/NetworkInfo;", [context](dx::IntrinsicContext& call) {
        const auto& facts = RequireOfflineConnectivity(call, context, true);
        if (call.arguments[0].AsInt() != 1 || !facts.wifi_type_supported) return dx::VmValue::Ref(dx::VmObjectRef{});
        return dx::VmValue::Ref(NewOfflineWifiNetworkInfo(call));
    });
    return std::move(builder).Build();
}

dx::VmObjectRef ConnectivityManagerForContext(dx::IntrinsicContext& call, const Context& context) {
    constexpr auto key = "connectivity";
    if (const auto found = context->singletons.find(key); found != context->singletons.end()) return found->second;
    const auto service = GetLocalServiceInterface(call.vm, key, "Landroid/net/IConnectivityManager$Stub;",
                                                  "Landroid/net/IConnectivityManager;");
    const auto service_root = call.vm.ProtectReferences(std::array{service});
    const auto name = CallAndroidMethod(call.vm, call.receiver, "getPackageName", "()Ljava/lang/String;").ref;
    const auto manager = NewPlatformObject(call.vm, "Landroid/net/ConnectivityManager;",
                                           "(Landroid/net/IConnectivityManager;Ljava/lang/String;)V",
                                           {dx::VmValue::Ref(service), dx::VmValue::Ref(name)});
    context->singletons.emplace(key, manager);
    return manager;
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

Decl Declare_local_WifiService(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalWifiService;",
                                                    "Landroid/net/wifi/IWifiManager$Stub;");
    builder.VirtualMethod("getWifiServiceMessenger", "()Landroid/os/Messenger;",
                          [](dx::IntrinsicContext&) { return dx::VmValue::Ref(dx::VmObjectRef{}); });
    builder.VirtualMethod("getWifiEnabledState", "()I", [context](dx::IntrinsicContext& call) {
        static_cast<void>(RequireOfflineConnectivity(call, context, false));
        return dx::VmValue::Int(1);  // WIFI_STATE_DISABLED
    });
    builder.VirtualMethod("setWifiEnabled", "(Z)Z", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.VirtualMethod("getConnectionInfo", "()Landroid/net/wifi/WifiInfo;", [context](dx::IntrinsicContext& call) {
        if (!context->granted_permissions.contains("android.permission.ACCESS_WIFI_STATE"))
            throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "ACCESS_WIFI_STATE is required"};
        static_cast<void>(RequireOfflineConnectivity(call, context, false));
        return dx::VmValue::Ref(NewPlatformObject(call.vm, "Landroid/net/wifi/WifiInfo;", "()V", {}));
    });
    builder.VirtualMethod("acquireWifiLock", "(Landroid/os/IBinder;ILjava/lang/String;Landroid/os/WorkSource;)Z",
                          [context](dx::IntrinsicContext& call) {
                              if (!context->granted_permissions.contains("android.permission.WAKE_LOCK"))
                                  throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "WifiLock requires WAKE_LOCK"};
                              const auto mode = call.arguments[1].AsInt();
                              if (mode < 1 || mode > 3 || call.arguments[3].ref.IsValid())
                                  throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                                        "unsupported WifiLock mode or work source"};
                              // There is no radio resource. The ROM manager owns its active-lock quota.
                              return dx::VmValue::Int(1);
                          });
    builder.VirtualMethod("releaseWifiLock", "(Landroid/os/IBinder;)Z",
                          [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    const auto multicast = [context](dx::IntrinsicContext&) {
        if (!context->granted_permissions.contains("android.permission.CHANGE_WIFI_MULTICAST_STATE"))
            throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
                                  "MulticastLock requires CHANGE_WIFI_MULTICAST_STATE"};
        return dx::VmValue::Void();
    };
    builder.VirtualMethod("acquireMulticastLock", "(Landroid/os/IBinder;Ljava/lang/String;)V", multicast);
    builder.VirtualMethod("releaseMulticastLock", "()V", multicast);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
