// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_net_ConnectivityManager.cpp ----
#include "catalog.h"

#include <charconv>

#include "ogplay/runtime/integration/apk_assets.h"
#include "ogplay/runtime/net/guest_network.h"

namespace ogplay::runtime::android_intrinsics {
namespace {
std::map<int, std::string> ReadFrameworkNetworkTypes(const DexVmAndroidContext& context) {
    const resources::AssetManager framework(FrameworkResourceApk(context));
    const auto& table = framework.resources();
    const auto id = table.identifierForName("networkAttributes", "array", "android");
    std::map<int, std::string> types;
    for (const auto& row : resources::getArrayStringResource(table, id)) {
        if (!row) continue;
        // NetworkConfig: name, network type, radio type, priority, restore time, dependency.
        const std::string entry(row->begin(), row->end());
        const auto first = entry.find(',');
        if (first == std::string::npos) continue;
        const auto second = entry.find(',', first + 1);
        if (second == std::string::npos) continue;
        int type{};
        const auto [end, error] = std::from_chars(entry.data() + first + 1, entry.data() + second, type);
        if (error != std::errc{} || end != entry.data() + second) continue;
        const auto name = entry.substr(0, first);
        if (type == 1 && name == "wifi")
            types.emplace(type, "WIFI");
        else if ((name == "mobile" || name.starts_with("mobile_")) && entry.substr(second + 1, 2) == "0,")
            types.emplace(type, name);
    }
    if (!types.contains(0) || !types.contains(1))
        throw resources::ResourceError("framework networkAttributes lacks mobile or Wi-Fi");
    return types;
}

std::map<int, std::string> SupportedNetworkTypes(const Context& context, AndroidOfflineConnectivity& facts) {
    if (facts.network_types.empty()) facts.network_types = ReadFrameworkNetworkTypes(*context);
    auto types = facts.network_types;
    if (!facts.wifi_type_supported) types.erase(1);
    return types;
}

AndroidOfflineConnectivity& RequireOfflineConnectivity(dx::IntrinsicContext& call, const Context& context,
                                                       bool network_permission) {
    if (network_permission) RequireServicePermission(context, "ACCESS_NETWORK_STATE");
    if (!context->offline_connectivity) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.connectivity_offline", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "connectivity facts are absent"};
    }
    return *context->offline_connectivity;
}

// Online mode: the device is on a connected Wi-Fi network while the user keeps Wi-Fi enabled.
bool WifiConnected(const Context& context) {
    return context->offline_connectivity && context->offline_connectivity->wifi_type_supported &&
           context->network != nullptr && context->network->Mode() == net::NetworkMode::online &&
           context->device_state.network.wifi_enabled;
}

constexpr std::string_view kWifiSsid = "OGPlay";

dx::VmValue CallAndroidStatic(dx::Interpreter& vm, const char* type, const char* name, const char* signature,
                              std::vector<dx::VmValue> arguments) {
    const auto id = vm.Linker().ResolveDescriptor(type);
    const auto initialized = vm.EnsureClassInitialized(id);
    if (initialized.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor, initialized.exception_message,
                              initialized.exception};
    const auto method = vm.Linker().FindDirectMethod(id, name, signature);
    if (!method) throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, name);
    const auto outcome = vm.Call(*method, arguments);
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    return outcome.value;
}

// The SDK compile stubs hide these constructors/setters. The native fact
// factory invokes the original BootDex methods; it does not reimplement them.
dx::VmObjectRef NewOfflineNetworkInfo(dx::IntrinsicContext& call, int network_type, std::string_view name,
                                      const bool connected = false) {
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
    const auto type_name = vm.NewStringUtf8(name);
    const auto type_name_root = vm.ProtectReferences(std::array{type_name});
    const auto subtype_name = vm.NewStringUtf8(network_type == 1 ? "" : "UNKNOWN");
    const auto strings_root = vm.ProtectReferences(std::array{subtype_name});
    const auto constructor = linker.FindDirectMethod(type, "<init>", "(IILjava/lang/String;Ljava/lang/String;)V");
    if (!constructor)
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "NetworkInfo constructor is unavailable");
    propagate(
        vm.Call(*constructor, std::array{dx::VmValue::Ref(info), dx::VmValue::Int(network_type), dx::VmValue::Int(0),
                                         dx::VmValue::Ref(type_name), dx::VmValue::Ref(subtype_name)}));
    const auto detailed_type = linker.ResolveDescriptor("Landroid/net/NetworkInfo$DetailedState;");
    propagate(vm.EnsureClassInitialized(detailed_type));
    const auto field = linker.FindFieldRecursive(detailed_type, connected ? "CONNECTED" : "DISCONNECTED",
                                                 "Landroid/net/NetworkInfo$DetailedState;");
    if (!field) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "NetworkInfo state is unavailable");
    const auto state = dx::VmObjectRef{linker.Class(detailed_type).static_storage[linker.Field(*field).slot]};
    // WifiStateMachine reports the quoted SSID as the connected network's extra info.
    const auto extra = connected ? vm.NewStringUtf8("\"" + std::string(kWifiSsid) + "\"") : dx::VmObjectRef{};
    const auto extra_root = vm.ProtectReferences(std::array{extra});
    static_cast<void>(CallAndroidMethod(
        vm, info, "setDetailedState", "(Landroid/net/NetworkInfo$DetailedState;Ljava/lang/String;Ljava/lang/String;)V",
        {dx::VmValue::Ref(state), dx::VmValue::Ref(dx::VmObjectRef{}), dx::VmValue::Ref(extra)}));
    static_cast<void>(CallAndroidMethod(vm, info, "setIsAvailable", "(Z)V", {dx::VmValue::Int(connected ? 1 : 0)}));
    return info;
}
}  // namespace

void EnsureLocalConnectivityBroadcast(dx::IntrinsicContext& call, const Context& context) {
    constexpr auto action = "android.net.conn.CONNECTIVITY_CHANGE";
    if (!context->offline_connectivity || !context->offline_connectivity->wifi_type_supported ||
        context->local_sticky_broadcasts.contains(action))
        return;
    const bool connected = WifiConnected(context);
    auto& vm = call.vm;
    const auto intent = NewPlatformObject(vm, "Landroid/content/Intent;", "(Ljava/lang/String;)V",
                                          {dx::VmValue::Ref(vm.NewStringUtf8(action))});
    const auto intent_root = vm.ProtectReferences(std::array{intent});
    const auto network = NewOfflineNetworkInfo(call, 1, "WIFI", connected);
    const auto network_root = vm.ProtectReferences(std::array{network});
    static_cast<void>(CallAndroidMethod(vm, intent, "putExtra",
                                        "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
                                        {MakeString(call, "networkInfo"), dx::VmValue::Ref(network)}));
    static_cast<void>(CallAndroidMethod(vm, intent, "putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;",
                                        {MakeString(call, "networkType"), dx::VmValue::Int(1)}));
    if (!connected)
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
        if (!WifiConnected(context)) return dx::VmValue::Ref(dx::VmObjectRef{});
        return dx::VmValue::Ref(NewOfflineNetworkInfo(call, 1, "WIFI", true));
    });
    const auto networks = [context](dx::IntrinsicContext& call) {
        return SupportedNetworkTypes(context, RequireOfflineConnectivity(call, context, true));
    };
    builder.VirtualMethod(
        "getNetworkInfo", "(I)Landroid/net/NetworkInfo;", [networks, context](dx::IntrinsicContext& call) {
            const auto types = networks(call);
            const auto type = types.find(call.arguments[0].AsInt());
            return dx::VmValue::Ref(type == types.end()
                                        ? dx::VmObjectRef{}
                                        : NewOfflineNetworkInfo(call, type->first, type->second,
                                                                type->first == 1 && WifiConnected(context)));
        });
    builder.VirtualMethod(
        "getAllNetworkInfo", "()[Landroid/net/NetworkInfo;", [networks, context](dx::IntrinsicContext& call) {
            const auto types = networks(call);
            auto& vm = call.vm;
            const auto array = NewServiceArray(vm, "[Landroid/net/NetworkInfo;", static_cast<JniSize>(types.size()));
            const auto root = vm.ProtectReferences(std::array{array});
            JniSize index = 0;
            for (const auto& [type, name] : types)
                vm.Model().SetObjectElement(
                    array, index++, NewOfflineNetworkInfo(call, type, name, type == 1 && WifiConnected(context)));
            return dx::VmValue::Ref(array);
        });
    builder.VirtualMethod("getProvisioningOrActiveNetworkInfo", "()Landroid/net/NetworkInfo;",
                          [context](dx::IntrinsicContext& call) {
                              static_cast<void>(RequireOfflineConnectivity(call, context, true));
                              if (!WifiConnected(context)) return dx::VmValue::Ref(dx::VmObjectRef{});
                              return dx::VmValue::Ref(NewOfflineNetworkInfo(call, 1, "WIFI", true));
                          });
    builder.VirtualMethod(
        "getActiveLinkProperties", "()Landroid/net/LinkProperties;", [context](dx::IntrinsicContext& call) {
            static_cast<void>(RequireOfflineConnectivity(call, context, true));
            if (!WifiConnected(context)) return dx::VmValue::Ref(dx::VmObjectRef{});
            const auto properties = NewPlatformObject(call.vm, "Landroid/net/LinkProperties;", "()V", {});
            const auto root = call.vm.ProtectReferences(std::array{properties});
            static_cast<void>(CallAndroidMethod(call.vm, properties, "setInterfaceName", "(Ljava/lang/String;)V",
                                                {MakeString(call, "wlan0")}));
            return dx::VmValue::Ref(properties);
        });
    builder.VirtualMethod("isNetworkSupported", "(I)Z", [networks](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(networks(call).contains(call.arguments[0].AsInt()));
    });
    builder.VirtualMethod(
        "getLinkProperties", "(I)Landroid/net/LinkProperties;", [networks](dx::IntrinsicContext& call) {
            if (!networks(call).contains(call.arguments[0].AsInt())) return dx::VmValue::Ref(dx::VmObjectRef{});
            return dx::VmValue::Ref(NewPlatformObject(call.vm, "Landroid/net/LinkProperties;", "()V", {}));
        });
    builder.VirtualMethod(
        "getAllNetworkState", "()[Landroid/net/NetworkState;", [networks, context](dx::IntrinsicContext& call) {
            const auto types = networks(call);
            auto& vm = call.vm;
            const auto array = NewServiceArray(vm, "[Landroid/net/NetworkState;", static_cast<JniSize>(types.size()));
            const auto array_root = vm.ProtectReferences(std::array{array});
            JniSize index = 0;
            for (const auto& [type, name] : types) {
                const auto info = NewOfflineNetworkInfo(call, type, name, type == 1 && WifiConnected(context));
                const auto info_root = vm.ProtectReferences(std::array{info});
                const auto properties = NewPlatformObject(vm, "Landroid/net/LinkProperties;", "()V", {});
                const auto properties_root = vm.ProtectReferences(std::array{properties});
                const auto capabilities = NewPlatformObject(vm, "Landroid/net/LinkCapabilities;", "()V", {});
                const auto state = NewPlatformObject(
                    vm, "Landroid/net/NetworkState;",
                    "(Landroid/net/NetworkInfo;Landroid/net/LinkProperties;Landroid/net/LinkCapabilities;)V",
                    {dx::VmValue::Ref(info), dx::VmValue::Ref(properties), dx::VmValue::Ref(capabilities)});
                vm.Model().SetObjectElement(array, index++, state);
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
             std::pair{"getActiveLinkQualityInfo", "()Landroid/net/LinkQualityInfo;"},
             std::pair{"getActiveNetworkQuotaInfo", "()Landroid/net/NetworkQuotaInfo;"},
             std::pair{"getLinkQualityInfo", "(I)Landroid/net/LinkQualityInfo;"},
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
        // Wi-Fi is unmetered; with no active network API 19 reports metered.
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "ACCESS_NETWORK_STATE");
            return dx::VmValue::Int(WifiConnected(context) ? 0 : 1);
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
        auto& vm = call.vm;
        const auto info = NewPlatformObject(vm, "Landroid/net/wifi/WifiInfo;", "()V", {});
        if (!WifiConnected(context)) return dx::VmValue::Ref(info);
        const auto root = vm.ProtectReferences(std::array{info});
        // The emulated access point: a completed association with the device address.
        const auto ssid = CallAndroidStatic(vm, "Landroid/net/wifi/WifiSsid;", "createFromAsciiEncoded",
                                            "(Ljava/lang/String;)Landroid/net/wifi/WifiSsid;",
                                            {MakeString(call, std::string(kWifiSsid))})
                              .ref;
        const auto ssid_root = vm.ProtectReferences(std::array{ssid});
        static_cast<void>(
            CallAndroidMethod(vm, info, "setSSID", "(Landroid/net/wifi/WifiSsid;)V", {dx::VmValue::Ref(ssid)}));
        static_cast<void>(
            CallAndroidMethod(vm, info, "setBSSID", "(Ljava/lang/String;)V", {MakeString(call, "02:00:00:00:00:01")}));
        static_cast<void>(CallAndroidMethod(vm, info, "setNetworkId", "(I)V", {dx::VmValue::Int(0)}));
        static_cast<void>(CallAndroidMethod(vm, info, "setRssi", "(I)V", {dx::VmValue::Int(-55)}));
        static_cast<void>(CallAndroidMethod(vm, info, "setLinkSpeed", "(I)V", {dx::VmValue::Int(72)}));
        static_cast<void>(CallAndroidMethod(vm, info, "setMacAddress", "(Ljava/lang/String;)V",
                                            {MakeString(call, "02:00:00:00:00:00")}));
        const auto& device = context->network->Config().device_ipv4;
        const auto bytes = vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte, 4);
        const auto bytes_root = vm.ProtectReferences(std::array{bytes});
        const std::array address{std::byte{device[0]}, std::byte{device[1]}, std::byte{device[2]},
                                 std::byte{device[3]}};
        vm.Model().WriteByteRegion(bytes, 0, address);
        const auto inet = CallAndroidStatic(vm, "Ljava/net/InetAddress;", "getByAddress", "([B)Ljava/net/InetAddress;",
                                            {dx::VmValue::Ref(bytes)})
                              .ref;
        const auto inet_root = vm.ProtectReferences(std::array{inet});
        static_cast<void>(
            CallAndroidMethod(vm, info, "setInetAddress", "(Ljava/net/InetAddress;)V", {dx::VmValue::Ref(inet)}));
        const auto completed_type = vm.Linker().ResolveDescriptor("Landroid/net/wifi/SupplicantState;");
        const auto initialized = vm.EnsureClassInitialized(completed_type);
        if (initialized.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor,
                                  initialized.exception_message, initialized.exception};
        const auto completed_field =
            vm.Linker().FindFieldRecursive(completed_type, "COMPLETED", "Landroid/net/wifi/SupplicantState;");
        if (completed_field) {
            const auto completed = dx::VmObjectRef{
                vm.Linker().Class(completed_type).static_storage[vm.Linker().Field(*completed_field).slot]};
            static_cast<void>(CallAndroidMethod(vm, info, "setSupplicantState", "(Landroid/net/wifi/SupplicantState;)V",
                                                {dx::VmValue::Ref(completed)}));
        }
        return dx::VmValue::Ref(info);
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
