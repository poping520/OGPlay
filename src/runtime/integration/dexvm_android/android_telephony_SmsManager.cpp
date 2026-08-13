#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_SmsManager(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/telephony/SmsManager;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("getDefault", "()Landroid/telephony/SmsManager;", handlers.handler_android_sms_get_default);
    builder.Virtual("sendTextMessage", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Landroid/app/PendingIntent;Landroid/app/PendingIntent;)V", handlers.handler_android_sms_send_text);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
