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
