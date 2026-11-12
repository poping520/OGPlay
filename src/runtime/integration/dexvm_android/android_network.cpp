// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_net_ConnectivityManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {
const AndroidOfflineConnectivity& RequireOfflineConnectivity(dx::IntrinsicContext& call, const Context& context,
                                                             bool network_permission) {
    if (network_permission) RequireServicePermission(context, "ACCESS_NETWORK_STATE");
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

void EnsureLocalConnectivityBroadcast(dx::IntrinsicContext& call, const Context& context) {
    constexpr auto action = "android.net.conn.CONNECTIVITY_CHANGE";
    if (!context->offline_connectivity || !context->offline_connectivity->wifi_type_supported ||
        context->network_policy.enabled || context->local_sticky_broadcasts.contains(action))
        return;
    auto& vm = call.vm;
    const auto intent = NewPlatformObject(vm, "Landroid/content/Intent;", "(Ljava/lang/String;)V",
                                          {dx::VmValue::Ref(vm.NewStringUtf8(action))});
    const auto intent_root = vm.ProtectReferences(std::array{intent});
    const auto network = NewOfflineWifiNetworkInfo(call);
    const auto network_root = vm.ProtectReferences(std::array{network});
    static_cast<void>(CallAndroidMethod(vm, intent, "putExtra",
                                        "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
                                        {MakeString(call, "networkInfo"), dx::VmValue::Ref(network)}));
    static_cast<void>(CallAndroidMethod(vm, intent, "putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;",
                                        {MakeString(call, "networkType"), dx::VmValue::Int(1)}));
    static_cast<void>(CallAndroidMethod(vm, intent, "putExtra", "(Ljava/lang/String;Z)Landroid/content/Intent;",
                                        {MakeString(call, "noConnectivity"), dx::VmValue::Int(1)}));
    context->local_sticky_broadcasts.try_emplace(action, intent);
}

Decl Declare_local_ConnectivityService_Class(const Context& context) {
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
    builder.VirtualMethod("getAllNetworkInfo", "()[Landroid/net/NetworkInfo;", [context](dx::IntrinsicContext& call) {
        const auto& facts = RequireOfflineConnectivity(call, context, true);
        auto& vm = call.vm;
        const auto array = NewServiceArray(vm, "[Landroid/net/NetworkInfo;", facts.wifi_type_supported ? 1 : 0);
        const auto root = vm.ProtectReferences(std::array{array});
        if (facts.wifi_type_supported) vm.Model().SetObjectElement(array, 0, NewOfflineWifiNetworkInfo(call));
        return dx::VmValue::Ref(array);
    });
    builder.VirtualMethod("isNetworkSupported", "(I)Z", [context](dx::IntrinsicContext& call) {
        const auto& facts = RequireOfflineConnectivity(call, context, true);
        return dx::VmValue::Int(call.arguments[0].AsInt() == 1 && facts.wifi_type_supported);
    });
    builder.VirtualMethod(
        "getLinkProperties", "(I)Landroid/net/LinkProperties;", [context](dx::IntrinsicContext& call) {
            const auto& facts = RequireOfflineConnectivity(call, context, true);
            if (call.arguments[0].AsInt() != 1 || !facts.wifi_type_supported)
                return dx::VmValue::Ref(dx::VmObjectRef{});
            return dx::VmValue::Ref(NewPlatformObject(call.vm, "Landroid/net/LinkProperties;", "()V", {}));
        });
    builder.VirtualMethod("getAllNetworkState", "()[Landroid/net/NetworkState;", [context](dx::IntrinsicContext& call) {
        const auto& facts = RequireOfflineConnectivity(call, context, true);
        auto& vm = call.vm;
        const auto array = NewServiceArray(vm, "[Landroid/net/NetworkState;", facts.wifi_type_supported ? 1 : 0);
        const auto array_root = vm.ProtectReferences(std::array{array});
        if (facts.wifi_type_supported) {
            const auto info = NewOfflineWifiNetworkInfo(call);
            const auto info_root = vm.ProtectReferences(std::array{info});
            const auto properties = NewPlatformObject(vm, "Landroid/net/LinkProperties;", "()V", {});
            const auto properties_root = vm.ProtectReferences(std::array{properties});
            const auto capabilities = NewPlatformObject(vm, "Landroid/net/LinkCapabilities;", "()V", {});
            const auto state = NewPlatformObject(
                vm, "Landroid/net/NetworkState;",
                "(Landroid/net/NetworkInfo;Landroid/net/LinkProperties;Landroid/net/LinkCapabilities;)V",
                {dx::VmValue::Ref(info), dx::VmValue::Ref(properties), dx::VmValue::Ref(capabilities)});
            vm.Model().SetObjectElement(array, 0, state);
        }
        return dx::VmValue::Ref(array);
    });
    for (const auto& [name, descriptor] : std::array{
             std::pair{"captivePortalCheckComplete", "(Landroid/net/NetworkInfo;)V"},
             std::pair{"captivePortalCheckCompleted", "(Landroid/net/NetworkInfo;Z)V"},
             std::pair{"checkMobileProvisioning", "(I)I"},
             std::pair{"findConnectionTypeForIface", "(Ljava/lang/String;)I"},
             std::pair{"getActiveNetworkInfoForUid", "(I)Landroid/net/NetworkInfo;"},
             std::pair{"getMobileProvisioningUrl", "()Ljava/lang/String;"},
             std::pair{"getMobileRedirectedProvisioningUrl", "()Ljava/lang/String;"},
             std::pair{"requestNetworkTransitionWakelock", "(Ljava/lang/String;)V"},
             std::pair{"setAirplaneMode", "(Z)V"},
             std::pair{"setDataDependency", "(IZ)V"},
             std::pair{"setGlobalProxy", "(Landroid/net/ProxyProperties;)V"},
             std::pair{"setProvisioningNotificationVisible", "(ZILjava/lang/String;Ljava/lang/String;)V"},
             std::pair{"supplyMessenger", "(ILandroid/os/Messenger;)V"},
         }) {
        DenyServiceMethod(builder, name, descriptor, "CONNECTIVITY_INTERNAL");
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"setPolicyDataEnable", "(IZ)V"},
         }) {
        DenyServiceMethod(builder, name, descriptor, "MANAGE_NETWORK_POLICY");
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"reportInetCondition", "(II)V"},
         }) {
        DenyServiceMethod(builder, name, descriptor, "STATUS_BAR");
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"markSocketAsUser", "(Landroid/os/ParcelFileDescriptor;I)V"},
         }) {
        DenyServiceMethod(builder, name, descriptor, "CONNECTIVITY_INTERNAL");
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getActiveLinkProperties", "()Landroid/net/LinkProperties;"},
             std::pair{"getActiveLinkQualityInfo", "()Landroid/net/LinkQualityInfo;"},
             std::pair{"getActiveNetworkQuotaInfo", "()Landroid/net/NetworkQuotaInfo;"},
             std::pair{"getLinkQualityInfo", "(I)Landroid/net/LinkQualityInfo;"},
             std::pair{"getProvisioningOrActiveNetworkInfo", "()Landroid/net/NetworkInfo;"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "ACCESS_NETWORK_STATE");
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getGlobalProxy", "()Landroid/net/ProxyProperties;"},
             std::pair{"getLegacyVpnInfo", "()Lcom/android/internal/net/LegacyVpnInfo;"},
             std::pair{"getVpnConfig", "()Lcom/android/internal/net/VpnConfig;"},
             std::pair{"establishVpn", "(Lcom/android/internal/net/VpnConfig;)Landroid/os/ParcelFileDescriptor;"},
         }) {
        builder.VirtualMethod(name, descriptor,
                              [](dx::IntrinsicContext&) { return dx::VmValue::Ref(dx::VmObjectRef{}); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"protectVpn", "(Landroid/os/ParcelFileDescriptor;)Z"},
             std::pair{"updateLockdownVpn", "()Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"isActiveNetworkMetered", "()Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "ACCESS_NETWORK_STATE");
            return dx::VmValue::Int(1);
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"isTetheringSupported", "()Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "ACCESS_NETWORK_STATE");
            return dx::VmValue::Int(0);
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getLastTetherError", "(Ljava/lang/String;)I"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "ACCESS_NETWORK_STATE");
            return dx::VmValue::Int(3);
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"tether", "(Ljava/lang/String;)I"},
             std::pair{"untether", "(Ljava/lang/String;)I"},
             std::pair{"setUsbTethering", "(Z)I"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "CHANGE_NETWORK_STATE");
            return dx::VmValue::Int(3);
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"requestRouteToHost", "(IILjava/lang/String;)Z"},
             std::pair{"requestRouteToHostAddress", "(I[BLjava/lang/String;)Z"},
             std::pair{"setRadio", "(IZ)Z"},
             std::pair{"setRadios", "(Z)Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "CHANGE_NETWORK_STATE");
            return dx::VmValue::Int(0);
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"startUsingNetworkFeature", "(ILjava/lang/String;Landroid/os/IBinder;)I"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "CHANGE_NETWORK_STATE");
            return dx::VmValue::Int(3);
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"stopUsingNetworkFeature", "(ILjava/lang/String;)I"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "CHANGE_NETWORK_STATE");
            return dx::VmValue::Int(-1);
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getAllLinkQualityInfo", "()[Landroid/net/LinkQualityInfo;"},
             std::pair{"getTetherableBluetoothRegexs", "()[Ljava/lang/String;"},
             std::pair{"getTetherableIfaces", "()[Ljava/lang/String;"},
             std::pair{"getTetherableUsbRegexs", "()[Ljava/lang/String;"},
             std::pair{"getTetherableWifiRegexs", "()[Ljava/lang/String;"},
             std::pair{"getTetheredIfaces", "()[Ljava/lang/String;"},
             std::pair{"getTetheringErroredIfaces", "()[Ljava/lang/String;"},
         }) {
        const auto element = std::string_view(descriptor).substr(std::string_view(descriptor).find(')') + 1);
        builder.VirtualMethod(name, descriptor, [context, element](dx::IntrinsicContext& call) {
            RequireServicePermission(context, "ACCESS_NETWORK_STATE");
            return dx::VmValue::Ref(NewServiceArray(call.vm, element.data()));
        });
    }

    builder.VirtualMethod("startLegacyVpn", "(Lcom/android/internal/net/VpnProfile;)V",
                          [context](dx::IntrinsicContext& call) -> dx::VmValue {
                              static_cast<void>(RequireOfflineConnectivity(call, context, true));
                              throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                                    "Missing active network connection"};
                          });
    builder.VirtualMethod("prepareVpn", "(Ljava/lang/String;Ljava/lang/String;)Z", [](dx::IntrinsicContext& call) {
        if (call.arguments[1].ref.IsValid())
            throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "CONTROL_VPN is required"};
        return dx::VmValue::Int(0);  // No package has been approved.
    });
    builder.VirtualMethod("getNetworkPreference", "()I", [context](dx::IntrinsicContext& call) {
        static_cast<void>(RequireOfflineConnectivity(call, context, true));
        return dx::VmValue::Int(context->device_state.network.preference);
    });
    builder.VirtualMethod("setNetworkPreference", "(I)V", [context](dx::IntrinsicContext& call) {
        RequireServicePermission(context, "CHANGE_NETWORK_STATE");
        const auto& facts = RequireOfflineConnectivity(call, context, false);
        if (facts.wifi_type_supported && call.arguments[0].AsInt() == 1) context->device_state.network.preference = 1;
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("getMobileDataEnabled", "()Z", [context](dx::IntrinsicContext& call) {
        static_cast<void>(RequireOfflineConnectivity(call, context, true));
        return dx::VmValue::Int(context->device_state.network.mobile_data_enabled);
    });
    builder.VirtualMethod("setMobileDataEnabled", "(Z)V", [context](dx::IntrinsicContext& call) {
        RequireServicePermission(context, "CHANGE_NETWORK_STATE");
        context->device_state.network.mobile_data_enabled = call.arguments[0].AsInt() != 0;
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

Decl Declare_local_WifiService_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalWifiService;",
                                                    "Landroid/net/wifi/IWifiManager$Stub;");
    builder.VirtualMethod("getWifiServiceMessenger", "()Landroid/os/Messenger;",
                          [](dx::IntrinsicContext&) { return dx::VmValue::Ref(dx::VmObjectRef{}); });
    builder.VirtualMethod("getWifiEnabledState", "()I", [context](dx::IntrinsicContext& call) {
        static_cast<void>(RequireOfflineConnectivity(call, context, false));
        RequireServicePermission(context, "ACCESS_WIFI_STATE");
        return dx::VmValue::Int(context->device_state.network.wifi_enabled ? 3 : 1);
    });
    builder.VirtualMethod("setWifiEnabled", "(Z)Z", [context](dx::IntrinsicContext& call) {
        RequireServicePermission(context, "CHANGE_WIFI_STATE");
        const auto& facts = RequireOfflineConnectivity(call, context, false);
        if (!facts.wifi_type_supported) return dx::VmValue::Int(0);
        context->device_state.network.wifi_enabled = call.arguments[0].AsInt() != 0;
        return dx::VmValue::Int(1);
    });
    builder.VirtualMethod("getConnectionInfo", "()Landroid/net/wifi/WifiInfo;", [context](dx::IntrinsicContext& call) {
        RequireServicePermission(context, "ACCESS_WIFI_STATE");
        static_cast<void>(RequireOfflineConnectivity(call, context, false));
        return dx::VmValue::Ref(NewPlatformObject(call.vm, "Landroid/net/wifi/WifiInfo;", "()V", {}));
    });
    builder.VirtualMethod(
        "acquireWifiLock", "(Landroid/os/IBinder;ILjava/lang/String;Landroid/os/WorkSource;)Z",
        [context](dx::IntrinsicContext& call) {
            RequireServicePermission(context, "WAKE_LOCK");
            const auto mode = call.arguments[1].AsInt();
            if (mode < 1 || mode > 3)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Invalid Wifi lock mode"};
            if (call.arguments[3].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "UPDATE_DEVICE_STATS is required"};
            context->device_state.network.wifi_locks.insert(dx::IntrinsicCall(call).NonNullRef(0, "binder"));
            return dx::VmValue::Int(1);
        });
    builder.VirtualMethod("releaseWifiLock", "(Landroid/os/IBinder;)Z", [context](dx::IntrinsicContext& call) {
        RequireServicePermission(context, "WAKE_LOCK");
        return dx::VmValue::Int(context->device_state.network.wifi_locks.erase(call.arguments[0].ref) != 0);
    });
    builder.VirtualMethod(
        "acquireMulticastLock", "(Landroid/os/IBinder;Ljava/lang/String;)V", [context](dx::IntrinsicContext& call) {
            RequireServicePermission(context, "CHANGE_WIFI_MULTICAST_STATE");
            context->device_state.network.multicast_locks.push_back(dx::IntrinsicCall(call).NonNullRef(0, "binder"));
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("releaseMulticastLock", "()V", [context](dx::IntrinsicContext&) {
        RequireServicePermission(context, "CHANGE_WIFI_MULTICAST_STATE");
        context->device_state.network.multicast_locks.clear();
        return dx::VmValue::Void();
    });
    for (const auto& [name, descriptor] : std::array{
             std::pair{"captivePortalCheckComplete", "()V"},
             std::pair{"setCountryCode", "(Ljava/lang/String;Z)V"},
             std::pair{"startWifi", "()V"},
             std::pair{"stopWifi", "()V"},
         }) {
        DenyServiceMethod(builder, name, descriptor, "CONNECTIVITY_INTERNAL");
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getBatchedScanResults", "(Ljava/lang/String;)Ljava/util/List;"},
             std::pair{"getConfiguredNetworks", "()Ljava/util/List;"},
             std::pair{"getScanResults", "(Ljava/lang/String;)Ljava/util/List;"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext& call) {
            RequireServicePermission(context, "ACCESS_WIFI_STATE");
            return dx::VmValue::Ref(NewPlatformObject(call.vm, "Ljava/util/ArrayList;", "()V", {}));
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"isDualBandSupported", "()Z"},
             std::pair{"isBatchedScanSupported", "()Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"isScanAlwaysAvailable", "()Z"},
             std::pair{"pingSupplicant", "()Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "ACCESS_WIFI_STATE");
            return dx::VmValue::Int(0);
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getFrequencyBand", "()I"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "ACCESS_WIFI_STATE");
            return dx::VmValue::Int(0);
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getWifiApEnabledState", "()I"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "ACCESS_WIFI_STATE");
            return dx::VmValue::Int(11);
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getDhcpInfo", "()Landroid/net/DhcpInfo;"},
             std::pair{"getWifiApConfiguration", "()Landroid/net/wifi/WifiConfiguration;"},
             std::pair{"getConfigFile", "()Ljava/lang/String;"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "ACCESS_WIFI_STATE");
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"disableNetwork", "(I)Z"},
             std::pair{"enableNetwork", "(IZ)Z"},
             std::pair{"removeNetwork", "(I)Z"},
             std::pair{"requestBatchedScan",
                       "(Landroid/net/wifi/BatchedScanSettings;Landroid/os/IBinder;Landroid/os/WorkSource;)Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "CHANGE_WIFI_STATE");
            return dx::VmValue::Int(0);
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"saveConfiguration", "()Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "CHANGE_WIFI_STATE");
            return dx::VmValue::Int(1);
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"disconnect", "()V"},
             std::pair{"reconnect", "()V"},
             std::pair{"reassociate", "()V"},
             std::pair{"pollBatchedScan", "()V"},
             std::pair{"setFrequencyBand", "(IZ)V"},
             std::pair{"stopBatchedScan", "(Landroid/net/wifi/BatchedScanSettings;)V"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "CHANGE_WIFI_STATE");
            return dx::VmValue::Void();
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"addOrUpdateNetwork", "(Landroid/net/wifi/WifiConfiguration;)I"},
             std::pair{"addToBlacklist", "(Ljava/lang/String;)V"},
             std::pair{"clearBlacklist", "()V"},
             std::pair{"setWifiApConfiguration", "(Landroid/net/wifi/WifiConfiguration;)V"},
             std::pair{"setWifiApEnabled", "(Landroid/net/wifi/WifiConfiguration;Z)V"},
         }) {
        const auto signature = std::string("android.net.wifi.IWifiManager.") + name + descriptor;
        builder.VirtualMethod(name, descriptor, [context, signature](dx::IntrinsicContext& call) -> dx::VmValue {
            RequireServicePermission(context, "CHANGE_WIFI_STATE");
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented(signature, 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", signature};
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"enableTdls", "(Ljava/lang/String;Z)V"},
             std::pair{"enableTdlsWithMacAddress", "(Ljava/lang/String;Z)V"},
         }) {
        RejectUnsupportedServiceMethod(builder, "android.net.wifi.IWifiManager", name, descriptor);
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getWifiStateMachineMessenger", "()Landroid/os/Messenger;"},
         }) {
        const auto signature = std::string("android.net.wifi.IWifiManager.") + name + descriptor;
        builder.VirtualMethod(name, descriptor, [context, signature](dx::IntrinsicContext& call) -> dx::VmValue {
            RequireServicePermission(context, "CHANGE_WIFI_STATE");
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented(signature, 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", signature};
        });
    }

    builder.VirtualMethod("startScan", "(Landroid/os/WorkSource;)V", [context](dx::IntrinsicContext& call) {
        RequireServicePermission(context, "CHANGE_WIFI_STATE");
        if (call.arguments[0].ref.IsValid())
            throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "UPDATE_DEVICE_STATS is required"};
        return dx::VmValue::Void();  // No visible access points.
    });
    builder.VirtualMethod("initializeMulticastFiltering", "()V", [context](dx::IntrinsicContext&) {
        RequireServicePermission(context, "CHANGE_WIFI_MULTICAST_STATE");
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("updateWifiLockWorkSource", "(Landroid/os/IBinder;Landroid/os/WorkSource;)V",
                          [context](dx::IntrinsicContext& call) {
                              RequireServicePermission(context, "WAKE_LOCK");
                              if (call.arguments[1].ref.IsValid())
                                  throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
                                                        "UPDATE_DEVICE_STATS is required"};
                              if (!context->device_state.network.wifi_locks.contains(call.arguments[0].ref))
                                  throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Wifi lock not active"};
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("isMulticastEnabled", "()Z", [context](dx::IntrinsicContext&) {
        RequireServicePermission(context, "ACCESS_WIFI_STATE");
        return dx::VmValue::Int(!context->device_state.network.multicast_locks.empty());
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
