// SMS sending is a non-goal surface: getDefault answers the cached
// singleton, sendTextMessage fails with accounting (UnsupportedNetwork).

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_SmsManager(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/telephony/SmsManager;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("getDefault", "()Landroid/telephony/SmsManager;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "sms",
                          "Landroid/telephony/SmsManager;"));
        });
    builder.Virtual("sendTextMessage",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Landroid/app/PendingIntent;Landroid/app/PendingIntent;)V",
        dx::IntrinsicHandler(UnsupportedNetwork));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
