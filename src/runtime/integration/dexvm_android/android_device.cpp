// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_provider_Settings_System.cpp ----
#include "catalog.h"

#include <type_traits>

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_location_LocationListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface(
        "Landroid/location/LocationListener;");
    builder.UnimplementedVirtual(
        "onLocationChanged", "(Landroid/location/Location;)V",
        dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual(
        "onStatusChanged",
        "(Ljava/lang/String;ILandroid/os/Bundle;)V",
        dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual(
        "onProviderEnabled", "(Ljava/lang/String;)V",
        dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual(
        "onProviderDisabled", "(Ljava/lang/String;)V",
        dx::kAccPublic | dx::kAccAbstract);
    return std::move(builder).Build();
}

Decl Declare_android_location_Criteria(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/location/Criteria;", "Ljava/lang/Object;",
        {"Landroid/os/Parcelable;"});
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

Decl Declare_android_location_Location(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/location/Location;", "Ljava/lang/Object;",
        {"Landroid/os/Parcelable;"});
    builder.Constructor("(Ljava/lang/String;)V", NeutralHandler('V'));
    return std::move(builder).Build();
}

Decl Declare_android_location_LocationManager(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/location/LocationManager;", "Ljava/lang/Object;");
    const auto unsupported_updates = [](dx::IntrinsicContext& call)
        -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) {
            ledger->RecordUnimplemented("dexvm.location_updates", 0);
        }
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            "location updates are outside the compatibility scope"};
    };
    builder.VirtualMethod(
        "getBestProvider",
        "(Landroid/location/Criteria;Z)Ljava/lang/String;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.VirtualMethod(
        "getLastKnownLocation",
        "(Ljava/lang/String;)Landroid/location/Location;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.VirtualMethod(
        "requestLocationUpdates",
        "(Ljava/lang/String;JFLandroid/location/LocationListener;"
        "Landroid/os/Looper;)V", unsupported_updates);
    builder.VirtualMethod(
        "removeUpdates", "(Landroid/location/LocationListener;)V",
        unsupported_updates);
    return std::move(builder).Build();
}

Decl Declare_android_provider_Settings_NameValueCache(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/provider/Settings$NameValueCache;", "Ljava/lang/Object;");
    const auto get_command = builder.BoundInstanceField(
        "mCallGetCommand", "Ljava/lang/String;", dx::kAccPrivate | dx::kAccFinal);
    const auto set_command = builder.BoundInstanceField(
        "mCallSetCommand", "Ljava/lang/String;", dx::kAccPrivate | dx::kAccFinal);
    const auto table_name = [](dx::IntrinsicContext& call,
                               const dx::IntrinsicFieldHandle command) {
        const auto value = dx::IntrinsicCall(call).GetRef(command);
        if (!value.IsValid()) return std::string{};
        const auto text = call.vm.StringUtf8(value);
        const auto separator = text.find('_');
        return separator == std::string::npos ? std::string{}
                                              : text.substr(separator + 1U);
    };
    const auto require_argument = [](dx::IntrinsicContext& call,
                                     const std::size_t index,
                                     const std::string_view name) {
        if (!call.arguments[index].ref.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  std::string(name) + " == null"};
        }
    };
    builder.FinalMethod(
        "getStringForUser",
        "(Landroid/content/ContentResolver;Ljava/lang/String;I)Ljava/lang/String;",
        [context, get_command, table_name,
         require_argument](dx::IntrinsicContext& call) {
            require_argument(call, 0U, "resolver");
            require_argument(call, 1U, "name");
            const auto name = call.vm.StringUtf8(call.arguments[1].ref);
            const auto table = table_name(call, get_command);
            if (table == "secure") {
                const auto found = context->secure_settings.find(name);
                return found == context->secure_settings.end()
                           ? dx::VmValue::Ref(dx::VmObjectRef{})
                           : MakeString(call, found->second);
            }
            const auto store = context->preferences.find(
                "__android.settings." + table);
            if (store == context->preferences.end()) {
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            const auto found = store->second.find(name);
            if (found == store->second.end()) {
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            const auto value = std::visit(
                [](const auto& item) {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, std::string>) return item;
                    else if constexpr (std::is_same_v<T, bool>) {
                        return std::string(item ? "1" : "0");
                    } else return std::to_string(item);
                }, found->second);
            return MakeString(call, value);
        });
    builder.FinalMethod(
        "putStringForUser",
        "(Landroid/content/ContentResolver;Ljava/lang/String;Ljava/lang/String;I)Z",
        [context, set_command, table_name,
         require_argument](dx::IntrinsicContext& call) {
            require_argument(call, 0U, "resolver");
            require_argument(call, 1U, "name");
            const auto table = table_name(call, set_command);
            if (table != "system") {
                if (auto* ledger = call.vm.Ledger()) {
                    ledger->RecordUnimplemented(
                        "dexvm.settings_privileged_write", 0);
                }
                return dx::VmValue::Int(0);
            }
            const auto name = call.vm.StringUtf8(call.arguments[1].ref);
            auto& store = context->preferences["__android.settings.system"];
            if (!call.arguments[2].ref.IsValid()) store.erase(name);
            else store[name] = call.vm.StringUtf8(call.arguments[2].ref);
            return dx::VmValue::Int(1);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_telephony_PhoneStateListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_PhoneStateListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/telephony/PhoneStateListener;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_telephony_SmsManager.cpp ----
// SMS sending is a non-goal surface: getDefault answers the cached
// singleton, sendTextMessage fails with accounting (UnsupportedNetwork).

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_SmsManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/telephony/SmsManager;", "Ljava/lang/Object;");
    builder.StaticMethod("getDefault", "()Landroid/telephony/SmsManager;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "sms",
                          "Landroid/telephony/SmsManager;"));
        });
    builder.FinalMethod("sendTextMessage",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Landroid/app/PendingIntent;Landroid/app/PendingIntent;)V",
        dx::IntrinsicHandler(UnsupportedNetwork));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_telephony_SmsMessage.cpp ----
// SMS is a non-goal surface: every entry point fails with accounting
// (UnsupportedNetwork) instead of pretending to succeed.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_SmsMessage(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/telephony/SmsMessage;", "Ljava/lang/Object;");
    builder.StaticMethod("createFromPdu", "([B)Landroid/telephony/SmsMessage;",
        dx::IntrinsicHandler(UnsupportedNetwork));
    builder.FinalMethod("getMessageBody", "()Ljava/lang/String;",
        dx::IntrinsicHandler(UnsupportedNetwork));
    builder.FinalMethod("getOriginatingAddress", "()Ljava/lang/String;",
        dx::IntrinsicHandler(UnsupportedNetwork));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_telephony_TelephonyManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_TelephonyManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/telephony/TelephonyManager;", "Ljava/lang/Object;");
    builder.FinalMethod("getDeviceId", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) { return MakeString(call, context->device_id); });
    builder.FinalMethod("getDeviceSoftwareVersion", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) { return MakeString(call, context->device_software_version); });
    builder.FinalMethod("getLine1Number", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) { return MakeString(call, context->line_number); });
    builder.FinalMethod("getNetworkOperator", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) { return MakeString(call, context->network_operator); });
    builder.FinalMethod("getNetworkOperatorName", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.FinalMethod("getNetworkCountryIso", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.FinalMethod("getSimCountryIso", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.FinalMethod("getSimOperator", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.FinalMethod("getSimOperatorName", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.FinalMethod("isNetworkRoaming", "()Z", TelephonyFalseHandler());
    builder.FinalMethod("getSimState", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);  // SIM_STATE_ABSENT
    });
    builder.FinalMethod("getPhoneType", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);  // PHONE_TYPE_NONE
    });
    builder.FinalMethod("listen", "(Landroid/telephony/PhoneStateListener;I)V",
        [context](dx::IntrinsicContext& call) {
            const auto listener = call.arguments[0].ref;
            const auto events = call.arguments[1].AsInt();
            if (!listener.IsValid()) {
                throw dx::DexVmError(dx::DexVmErrorReason::invalid_operand,
                    "TelephonyManager.listen requires a listener");
            }
            if (events == 0) context->telephony_listeners.erase(listener.Value());
            else context->telephony_listeners[listener.Value()] = events;
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
