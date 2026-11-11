// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_provider_Settings_System.cpp ----
#include "catalog.h"

#include <type_traits>

namespace ogplay::runtime::android_intrinsics {

dx::VmObjectRef NfcManagerForContext(dx::IntrinsicContext& call, const Context& context) {
    const auto found = context->singletons.find("nfc");
    if (found != context->singletons.end()) return found->second;
    auto& vm = call.vm;
    const auto type = vm.Linker().ResolveDescriptor("Landroid/nfc/NfcManager;");
    const auto require = [&](const dx::VmCallOutcome& outcome) {
        if (outcome.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
    };
    require(vm.EnsureClassInitialized(type));
    const auto instance = vm.NewIntrinsicInstance("Landroid/nfc/NfcManager;");
    const auto roots = vm.ProtectReferences(std::array{instance});
    const auto constructor = vm.Linker().FindDirectMethod(type, "<init>", "(Landroid/content/Context;)V");
    if (!constructor)
        throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, "NfcManager constructor is missing");
    require(vm.Call(*constructor, std::array{dx::VmValue::Ref(instance), dx::VmValue::Ref(call.receiver)}));
    context->singletons.emplace("nfc", instance);
    return instance;
}

Decl Declare_local_UsbService(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalUsbService;",
                                                    "Landroid/hardware/usb/IUsbManager$Stub;");
    const auto no_transport = [](dx::IntrinsicContext& call) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.usb_transport", 0);
    };
    const auto denied = [no_transport](dx::IntrinsicContext& call) {
        no_transport(call);
        return dx::VmValue::Int(0);
    };
    builder.VirtualMethod("getDeviceList", "(Landroid/os/Bundle;)V", [](dx::IntrinsicContext& call) {
        static_cast<void>(dx::IntrinsicCall(call).NonNullRef(0, "devices"));
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("getCurrentAccessory", "()Landroid/hardware/usb/UsbAccessory;",
                          [](dx::IntrinsicContext&) { return dx::VmValue::Ref(dx::VmObjectRef{}); });
    builder.VirtualMethod("hasDevicePermission", "(Landroid/hardware/usb/UsbDevice;)Z", denied);
    builder.VirtualMethod("hasAccessoryPermission", "(Landroid/hardware/usb/UsbAccessory;)Z", denied);
    builder.VirtualMethod("openDevice", "(Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;",
                          [no_transport](dx::IntrinsicContext& call) {
                              no_transport(call);
                              return dx::VmValue::Ref(dx::VmObjectRef{});
                          });
    for (const auto& [name, descriptor] : std::array{
             std::pair{"openAccessory", "(Landroid/hardware/usb/UsbAccessory;)Landroid/os/ParcelFileDescriptor;"},
             std::pair{"requestAccessoryPermission",
                       "(Landroid/hardware/usb/UsbAccessory;Ljava/lang/String;Landroid/app/PendingIntent;)V"},
             std::pair{"requestDevicePermission",
                       "(Landroid/hardware/usb/UsbDevice;Ljava/lang/String;Landroid/app/PendingIntent;)V"},
             std::pair{"setCurrentFunction", "(Ljava/lang/String;Z)V"},
             std::pair{"setMassStorageBackingFile", "(Ljava/lang/String;)V"},
         }) {
        RejectUnsupportedServiceMethod(builder, "android.hardware.usb.IUsbManager", name, descriptor);
    }
    return std::move(builder).Build();
}

dx::VmObjectRef UsbManagerForContext(dx::IntrinsicContext& call, const Context& context) {
    constexpr auto key = "usb";
    if (const auto found = context->singletons.find(key); found != context->singletons.end()) return found->second;
    const auto service = GetLocalServiceInterface(call.vm, key, "Landroid/hardware/usb/IUsbManager$Stub;",
                                                  "Landroid/hardware/usb/IUsbManager;");
    const auto instance = NewPlatformObject(call.vm, "Landroid/hardware/usb/UsbManager;",
                                            "(Landroid/content/Context;Landroid/hardware/usb/IUsbManager;)V",
                                            {dx::VmValue::Ref(call.receiver), dx::VmValue::Ref(service)});
    context->singletons.emplace(key, instance);
    return instance;
}

Decl Declare_local_LocationService(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalLocationService;",
                                                    "Landroid/location/ILocationManager$Stub;");
    builder.VirtualMethod("getBestProvider", "(Landroid/location/Criteria;Z)Ljava/lang/String;",
                          [](dx::IntrinsicContext&) { return dx::VmValue::Ref(dx::VmObjectRef{}); });
    builder.VirtualMethod("getLastLocation",
                          "(Landroid/location/LocationRequest;Ljava/lang/String;)Landroid/location/Location;",
                          [](dx::IntrinsicContext&) { return dx::VmValue::Ref(dx::VmObjectRef{}); });
    RejectUnsupportedServiceMethod(
        builder, "android.location.ILocationManager", "requestLocationUpdates",
        "(Landroid/location/LocationRequest;Landroid/location/ILocationListener;Landroid/app/PendingIntent;Ljava/lang/String;)V");
    RejectUnsupportedServiceMethod(
        builder, "android.location.ILocationManager", "removeUpdates",
        "(Landroid/location/ILocationListener;Landroid/app/PendingIntent;Ljava/lang/String;)V");
    return std::move(builder).Build();
}

dx::VmObjectRef LocationManagerForContext(dx::IntrinsicContext& call, const Context& context) {
    constexpr auto key = "location";
    if (const auto found = context->singletons.find(key); found != context->singletons.end()) return found->second;
    const auto service = GetLocalServiceInterface(call.vm, key, "Landroid/location/ILocationManager$Stub;",
                                                  "Landroid/location/ILocationManager;");
    const auto manager = NewPlatformObject(call.vm, "Landroid/location/LocationManager;",
                                           "(Landroid/content/Context;Landroid/location/ILocationManager;)V",
                                           {dx::VmValue::Ref(call.receiver), dx::VmValue::Ref(service)});
    context->singletons.emplace(key, manager);
    return manager;
}

Decl Declare_local_SettingsProvider(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalSettingsProvider;",
                                                    "Landroid/os/Binder;", {"Landroid/content/IContentProvider;"});
    builder.Constructor("()V", [](dx::IntrinsicContext& call) {
        const auto parent = call.vm.Linker().ResolveDescriptor("Landroid/os/Binder;");
        const auto init = call.vm.Linker().FindDirectMethod(parent, "<init>", "()V");
        if (!init) throw std::logic_error("Binder constructor is unavailable");
        const auto result = call.vm.Call(*init, std::array{dx::VmValue::Ref(call.receiver)});
        if (result.exception.IsValid()) call.vm.SetPendingException(result.exception);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("asBinder", "()Landroid/os/IBinder;",
                          [](dx::IntrinsicContext& call) { return dx::VmValue::Ref(call.receiver); });
    builder.VirtualMethod(
        "call", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Landroid/os/Bundle;)Landroid/os/Bundle;",
        [context](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            dx::IntrinsicCall args(call);
            const auto command = vm.StringUtf8(args.NonNullRef(1, "method"));
            const auto key = args.NonNullRef(2, "name");
            const auto name = vm.StringUtf8(key);
            const auto extras = call.arguments[3].ref;
            if (extras.IsValid()) {
                const auto user = CallAndroidMethod(vm, extras, "getInt", "(Ljava/lang/String;I)I",
                                                    {MakeString(call, "_user"), dx::VmValue::Int(0)})
                                      .AsInt();
                if (user != 0)
                    throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "settings access outside the current user"};
            }
            if (command == "PUT_secure" || command == "PUT_global") {
                if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.settings_privileged_write", 0);
                throw dx::VmJavaThrow{"Landroid/os/RemoteException;", "privileged settings write"};
            }
            if (command == "PUT_system") {
                const auto value_ref =
                    CallAndroidMethod(vm, extras, "getString", "(Ljava/lang/String;)Ljava/lang/String;",
                                      {MakeString(call, "value")})
                        .ref;
                auto& store = context->preferences["__android.settings.system"];
                const auto old = store.find(name);
                const auto value = value_ref.IsValid() ? vm.StringUtf8(value_ref) : std::string{};
                if (value_ref.IsValid()) {
                    if (old != store.end() && std::get<std::string>(old->second) == value)
                        return dx::VmValue::Ref(dx::VmObjectRef{});
                    store.insert_or_assign(name, value);
                } else if (store.erase(name) == 0) {
                    return dx::VmValue::Ref(dx::VmObjectRef{});
                }
                ++context->settings_system_version;
                const auto type = vm.Linker().ResolveDescriptor("Landroid/provider/Settings$System;");
                const auto method =
                    vm.Linker().FindDirectMethod(type, "getUriFor", "(Ljava/lang/String;)Landroid/net/Uri;");
                if (!method) throw std::logic_error("Settings.System.getUriFor is unavailable");
                const auto result = vm.Call(*method, std::array{dx::VmValue::Ref(key)});
                if (result.exception.IsValid()) {
                    vm.SetPendingException(result.exception);
                    return dx::VmValue::Ref(dx::VmObjectRef{});
                }
                const auto roots = vm.ProtectReferences(std::array{result.value.ref});
                NotifyLocalContentChange(vm, context, result.value.ref, dx::VmObjectRef{}, false);
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }

            dx::VmValue value = dx::VmValue::Ref(dx::VmObjectRef{});
            if (command == "GET_secure") {
                const auto found = context->secure_settings.find(name);
                if (found != context->secure_settings.end()) value = MakeString(call, found->second);
            } else if (command == "GET_system" || command == "GET_global") {
                const auto store = context->preferences.find("__android.settings." + command.substr(4));
                if (store != context->preferences.end()) {
                    const auto found = store->second.find(name);
                    if (found != store->second.end()) {
                        value = MakeString(call, std::visit(
                                                     [](const auto& item) -> std::string {
                                                         using T = std::decay_t<decltype(item)>;
                                                         if constexpr (std::is_same_v<T, std::string>)
                                                             return item;
                                                         else if constexpr (std::is_same_v<T, bool>)
                                                             return item ? "1" : "0";
                                                         else
                                                             return std::to_string(item);
                                                     },
                                                     found->second));
                    }
                }
            } else {
                if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.settings_provider.call", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", command};
            }
            const auto value_root = vm.ProtectReferences(std::array{value.ref});
            const auto result = NewPlatformObject(vm, "Landroid/os/Bundle;", "()V", {});
            const auto result_root = vm.ProtectReferences(std::array{result});
            static_cast<void>(CallAndroidMethod(vm, result, "putString", "(Ljava/lang/String;Ljava/lang/String;)V",
                                                {MakeString(call, "value"), value}));
            return dx::VmValue::Ref(result);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

Decl Declare_local_SmsService(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalSmsService;",
                                                    "Lcom/android/internal/telephony/ISms$Stub;");
    for (
        const auto& [name, descriptor] : std::array{
            std::pair{"copyMessageToIccEf", "(Ljava/lang/String;I[B[B)Z"},
            std::pair{"disableCellBroadcast", "(I)Z"},
            std::pair{"disableCellBroadcastRange", "(II)Z"},
            std::pair{"enableCellBroadcast", "(I)Z"},
            std::pair{"enableCellBroadcastRange", "(II)Z"},
            std::pair{"getAllMessagesFromIccEf", "(Ljava/lang/String;)Ljava/util/List;"},
            std::pair{"getImsSmsFormat", "()Ljava/lang/String;"},
            std::pair{"getPremiumSmsPermission", "(Ljava/lang/String;)I"},
            std::pair{"isImsSmsSupported", "()Z"},
            std::pair{
                "sendData",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I[BLandroid/app/PendingIntent;Landroid/app/PendingIntent;)V"},
            std::pair{
                "sendMultipartText",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/util/List;Ljava/util/List;Ljava/util/List;)V"},
            std::pair{
                "sendText",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Landroid/app/PendingIntent;Landroid/app/PendingIntent;)V"},
            std::pair{"setPremiumSmsPermission", "(Ljava/lang/String;I)V"},
            std::pair{"updateMessageOnIccEf", "(Ljava/lang/String;II[B)Z"},
        })
        RejectUnsupportedServiceMethod(builder, "com.android.internal.telephony.ISms", name, descriptor);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_telephony_TelephonyManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_local_PhoneSubscriberService(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalPhoneSubscriberService;",
                                                    "Lcom/android/internal/telephony/IPhoneSubInfo$Stub;");
    builder.VirtualMethod("getDeviceId", "()Ljava/lang/String;",
                          [context](dx::IntrinsicContext& call) { return MakeString(call, context->device_id); });
    builder.VirtualMethod("getDeviceSvn", "()Ljava/lang/String;", [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->device_software_version);
    });
    builder.VirtualMethod("getLine1Number", "()Ljava/lang/String;",
                          [context](dx::IntrinsicContext& call) { return MakeString(call, context->line_number); });
    builder.VirtualMethod("getSubscriberId", "()Ljava/lang/String;",
                          [](dx::IntrinsicContext&) { return dx::VmValue::Ref(dx::VmObjectRef{}); });
    return std::move(builder).Build();
}

Decl Declare_local_TelephonyService(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalTelephonyService;",
                                                    "Lcom/android/internal/telephony/ITelephony$Stub;");
    builder.VirtualMethod("getActivePhoneType", "()I", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    return std::move(builder).Build();
}

Decl Declare_local_TelephonyRegistry(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalTelephonyRegistry;",
                                                    "Lcom/android/internal/telephony/ITelephonyRegistry$Stub;");
    builder.VirtualMethod("listen", "(Ljava/lang/String;Lcom/android/internal/telephony/IPhoneStateListener;IZ)V",
                          [context](dx::IntrinsicContext& call) {
                              const auto callback = dx::IntrinsicCall(call).NonNullRef(1, "callback");
                              const auto events = call.arguments[2].AsInt();
                              // The absent cellular device never produces state changes. Keep only
                              // weak registrations; the ROM listener owns its local Binder callback.
                              if (events == 0)
                                  context->telephony_listeners.erase(callback.Value());
                              else
                                  context->telephony_listeners[callback.Value()] = events;
                              return dx::VmValue::Void();
                          });
    return std::move(builder).Build();
}

dx::VmObjectRef TelephonyManagerForContext(dx::IntrinsicContext& call, const Context& context) {
    constexpr auto key = "phone";
    if (const auto found = context->singletons.find(key); found != context->singletons.end()) return found->second;
    const auto manager = NewPlatformObject(call.vm, "Landroid/telephony/TelephonyManager;",
                                           "(Landroid/content/Context;)V", {dx::VmValue::Ref(call.receiver)});
    context->singletons.emplace(key, manager);
    return manager;
}

}  // namespace ogplay::runtime::android_intrinsics
