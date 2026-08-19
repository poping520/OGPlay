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
