// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_provider_Settings_System.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_provider_Settings_System(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/provider/Settings$System;", "Ljava/lang/Object;");
    // System settings table shares the session-lifetime preference store.
    builder.StaticMethod("getInt", "(Landroid/content/ContentResolver;Ljava/lang/String;I)I",
        [context](dx::IntrinsicContext& call) {
            const auto key = call.vm.StringUtf8(call.arguments[1].ref);
            auto& store = context->preferences["__android.settings.system"];
            const auto found = store.find(key);
            if (found != store.end()) {
                if (const auto* value = std::get_if<std::int32_t>(
                        &found->second)) {
                    return dx::VmValue::Int(*value);
                }
            }
            return dx::VmValue::Int(call.arguments[2].AsInt());
        });
    builder.StaticMethod("putInt", "(Landroid/content/ContentResolver;Ljava/lang/String;I)Z",
        [context](dx::IntrinsicContext& call) {
            const auto key = call.vm.StringUtf8(call.arguments[1].ref);
            context->preferences["__android.settings.system"][key] =
                call.arguments[2].AsInt();
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
