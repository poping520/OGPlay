#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_TelephonyManager(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/telephony/TelephonyManager;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getDeviceId", "()Ljava/lang/String;", handlers.handler_android_telephony_get_device_id);
    builder.Virtual("getDeviceSoftwareVersion", "()Ljava/lang/String;", handlers.handler_android_telephony_get_software_version);
    builder.Virtual("getLine1Number", "()Ljava/lang/String;", handlers.handler_android_telephony_get_line1_number);
    builder.Virtual("getNetworkOperator", "()Ljava/lang/String;", handlers.handler_android_telephony_get_network_operator);
    builder.Virtual("getNetworkOperatorName", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.Virtual("getNetworkCountryIso", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.Virtual("getSimCountryIso", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.Virtual("getSimOperator", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.Virtual("getSimOperatorName", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    builder.Virtual("isNetworkRoaming", "()Z", TelephonyFalseHandler());
    builder.Virtual("getSimState", "()I", handlers.handler_android_telephony_get_sim_state);
    builder.Virtual("getPhoneType", "()I", handlers.handler_android_telephony_get_phone_type);
    builder.Virtual("listen", "(Landroid/telephony/PhoneStateListener;I)V", handlers.handler_android_telephony_listen);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
