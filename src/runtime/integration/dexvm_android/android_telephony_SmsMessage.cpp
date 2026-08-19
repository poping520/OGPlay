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
