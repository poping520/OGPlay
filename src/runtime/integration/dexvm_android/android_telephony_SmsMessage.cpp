// SMS is a non-goal surface: every entry point fails with accounting
// (UnsupportedNetwork) instead of pretending to succeed.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_SmsMessage(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/telephony/SmsMessage;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("createFromPdu", "([B)Landroid/telephony/SmsMessage;",
        dx::IntrinsicHandler(UnsupportedNetwork));
    builder.Virtual("getMessageBody", "()Ljava/lang/String;",
        dx::IntrinsicHandler(UnsupportedNetwork));
    builder.Virtual("getOriginatingAddress", "()Ljava/lang/String;",
        dx::IntrinsicHandler(UnsupportedNetwork));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
