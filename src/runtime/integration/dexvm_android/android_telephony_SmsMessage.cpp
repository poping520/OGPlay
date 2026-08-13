#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_SmsMessage(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/telephony/SmsMessage;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("createFromPdu", "([B)Landroid/telephony/SmsMessage;", handlers.handler_android_sms_create_from_pdu);
    builder.Virtual("getMessageBody", "()Ljava/lang/String;", handlers.handler_android_sms_get_message_body);
    builder.Virtual("getOriginatingAddress", "()Ljava/lang/String;", handlers.handler_android_sms_get_originating_address);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
