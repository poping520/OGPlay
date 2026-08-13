#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_TelephonyManager(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/telephony/TelephonyManager;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getDeviceId", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) { return MakeString(call, context->device_id); });
    builder.Virtual("getDeviceSoftwareVersion", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) { return MakeString(call, context->device_software_version); });
    builder.Virtual("getLine1Number", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) { return MakeString(call, context->line_number); });
    builder.Virtual("getNetworkOperator", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) { return MakeString(call, context->network_operator); });
    builder.Virtual("getNetworkOperatorName", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.Virtual("getNetworkCountryIso", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.Virtual("getSimCountryIso", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.Virtual("getSimOperator", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.Virtual("getSimOperatorName", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.Virtual("isNetworkRoaming", "()Z", TelephonyFalseHandler());
    builder.Virtual("getSimState", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);  // SIM_STATE_ABSENT
    });
    builder.Virtual("getPhoneType", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);  // PHONE_TYPE_NONE
    });
    builder.Virtual("listen", "(Landroid/telephony/PhoneStateListener;I)V",
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
